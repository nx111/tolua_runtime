#!/usr/bin/env python3
"""
Scan LuaJIT bytecode with the same conservative FR1->FR2 checks used by the
experimental converter and print the first failing proto per file.
"""

from __future__ import annotations

import argparse
import copy
import os
import re
import subprocess
import sys
from collections import Counter


INST_RE = re.compile(r"^(\d{4})(?: =>)?\s+([A-Z0-9_]+)\s*(.*)$")
BYTECODE_RE = re.compile(r"^-- BYTECODE -- (.*)$")
NUM_RE = re.compile(r"-?\d+")
FOCUS_OPS = ("CALLM", "CALLMT", "RETM", "TSETM", "ISNEXT", "ITERN", "ITERC", "VARG", "UCLO")


class ScanError(RuntimeError):
    pass


def run_bl(luajit: str, cwd: str, path: str) -> str:
    env = os.environ.copy()
    env["LUA_PATH"] = ".\\?.lua;.\\?\\init.lua;;"
    proc = subprocess.run(
        [luajit, "-bl", path],
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if proc.returncode != 0:
        raise ScanError(f"luajit -bl failed for {path}: {proc.stdout.strip()}")
    return proc.stdout


def parse_bytecode(text: str) -> list[dict]:
    protos = []
    current = None
    header = None

    for raw in text.splitlines():
        match = BYTECODE_RE.match(raw)
        if match:
            if current is not None:
                protos.append({"header": header, "ins": current})
            current = []
            header = match.group(1)
            continue

        if current is None:
            continue

        match = INST_RE.match(raw)
        if not match:
            continue

        rest = match.group(3)
        before_comment = rest.split(";", 1)[0]
        target = None
        if "=>" in before_comment:
            jump = re.search(r"=>\s*(\d+)", before_comment)
            if jump:
                target = int(jump.group(1))
        nums = [int(value) for value in NUM_RE.findall(before_comment.split("=>")[0])]
        current.append(
            {
                "pc": int(match.group(1)),
                "op": match.group(2),
                "nums": nums,
                "target": target,
                "raw": raw.rstrip(),
            }
        )

    if current is not None:
        protos.append({"header": header, "ins": current})

    return protos


def find_range_hole(holes: set[int], first: int, last: int) -> int | None:
    if last <= first:
        return None
    first = max(first, 0)
    last = min(last, 255)
    for reg in range(first, last):
        if reg in holes:
            return reg
    return None


def collapse_multires(proto: list[dict], index: int) -> None:
    if index == 0:
        raise ScanError(f"pc {proto[index]['pc']}: open-result consumer has no preceding producer")

    producer_index = index - 1
    prev = proto[producer_index]
    if prev["op"] == "UCLO":
        if producer_index == 0:
            raise ScanError(f"pc {prev['pc']}: UCLO before open-result consumer has no preceding producer")
        if prev["target"] != proto[index]["pc"]:
            raise ScanError(
                f"pc {prev['pc']}: UCLO before open-result consumer jumps to {prev['target']} "
                f"instead of pc {proto[index]['pc']}"
            )
        producer_index -= 1
        prev = proto[producer_index]

    if prev["op"] not in ("CALL", "VARG"):
        raise ScanError(f"pc {prev['pc']}: preceding {prev['op']} does not support single-result downgrade")
    if len(prev["nums"]) < 2 or prev["nums"][1] != 0:
        raise ScanError(f"pc {prev['pc']}: preceding {prev['op']} is not open-result (B=0)")

    prev["nums"][1] = 2


def prepare_proto(proto: list[dict]) -> list[dict]:
    proto = copy.deepcopy(proto)
    pc_index = {ins["pc"]: idx for idx, ins in enumerate(proto)}

    for idx, ins in enumerate(proto):
        op = ins["op"]
        if op == "CALLM":
            a, b, c = ins["nums"]
            if c + 2 > 255:
                raise ScanError(f"pc {ins['pc']}: CALLM argument count overflows when downgraded to CALL")
            collapse_multires(proto, idx)
            ins["op"] = "CALL"
            ins["nums"] = [a, b if b != 0 else 2, c + 2]
        elif op == "CALLMT":
            a, d = ins["nums"]
            if d + 2 > 65535:
                raise ScanError(f"pc {ins['pc']}: CALLMT argument count overflows when downgraded to CALLT")
            collapse_multires(proto, idx)
            ins["op"] = "CALLT"
            ins["nums"] = [a, d + 2]
        elif op == "ISNEXT":
            target = ins["target"]
            if target not in pc_index:
                raise ScanError(f"pc {ins['pc']}: ISNEXT target {target} is outside the proto")
            target_op = proto[pc_index[target]]["op"]
            if target_op not in ("ITERN", "ITERC"):
                raise ScanError(f"pc {ins['pc']}: ISNEXT target pc={target} is {target_op}")
            ins["op"] = "JMP"
        elif op == "ITERN":
            ins["op"] = "ITERC"

    return proto


def validate_proto(proto: list[dict]) -> tuple[set[int], int]:
    holes = {ins["nums"][0] for ins in proto if ins["op"] in ("CALL", "CALLT", "ITERC") and ins["nums"]}
    pc_index = {ins["pc"]: idx for idx, ins in enumerate(proto)}
    max_reg = -1

    for ins in proto:
        for num in ins["nums"]:
            if 0 <= num <= 255:
                max_reg = max(max_reg, num)

        op = ins["op"]
        nums = ins["nums"]
        if op == "CALL":
            a, b, c = nums
            if c == 0:
                raise ScanError(f"pc {ins['pc']}: CALL must have at least one encoded argument slot")
            if b == 0:
                idx = pc_index[ins["pc"]]
                if idx + 1 >= len(proto):
                    raise ScanError(f"pc {ins['pc']}: CALL with open results must be followed by TSETM")
                nxt = proto[idx + 1]
                if nxt["op"] != "TSETM":
                    raise ScanError(
                        f"pc {ins['pc']}: CALL with open results is only supported when immediately consumed by TSETM"
                    )
                if not nxt["nums"] or nxt["nums"][0] != a:
                    raise ScanError(f"pc {ins['pc']}: CALL open-result base mismatch with following TSETM")
                hole = find_range_hole(holes, a + 1, a + c - 1)
                if hole is not None:
                    raise ScanError(
                        f"pc {ins['pc']}: CALL open-result arguments in [{a + 1},{a + c - 1}) "
                        f"cross FR2 hole at register {hole}"
                    )
            else:
                hole = find_range_hole(holes, a + 1, a + b - 2)
                if hole is not None:
                    raise ScanError(
                        f"pc {ins['pc']}: CALL fixed results in [{a + 1},{a + b - 2}) "
                        f"cross FR2 hole at register {hole}"
                    )
                hole = find_range_hole(holes, a + 1, a + c - 1)
                if hole is not None:
                    raise ScanError(
                        f"pc {ins['pc']}: CALL argument range [{a + 1},{a + c - 1}) "
                        f"cross FR2 hole at register {hole}"
                    )
        elif op == "CALLT":
            a, d = nums
            if d == 0:
                raise ScanError(f"pc {ins['pc']}: CALLT must have at least one encoded argument slot")
            hole = find_range_hole(holes, a + 1, a + d - 1)
            if hole is not None:
                raise ScanError(
                    f"pc {ins['pc']}: CALLT argument range [{a + 1},{a + d - 1}) "
                    f"cross FR2 hole at register {hole}"
                )
        elif op == "ITERC":
            a, b, c = nums
            if b == 0:
                raise ScanError(f"pc {ins['pc']}: ITERC with open results (B=0) is not supported")
            if c != 3:
                raise ScanError(f"pc {ins['pc']}: ITERC expects C=3, got {c}")
            hole = find_range_hole(holes, a + 1, a + b - 2)
            if hole is not None:
                raise ScanError(
                    f"pc {ins['pc']}: ITERC fixed results in [{a + 1},{a + b - 2}) "
                    f"cross FR2 hole at register {hole}"
                )
            hole = find_range_hole(holes, a + 1, a + c - 1)
            if hole is not None:
                raise ScanError(
                    f"pc {ins['pc']}: ITERC iterator arguments in [{a + 1},{a + c - 1}) "
                    f"cross FR2 hole at register {hole}"
                )
        elif op == "VARG":
            a, b, _ = nums
            if b == 0:
                idx = pc_index[ins["pc"]]
                if idx + 1 >= len(proto):
                    raise ScanError(f"pc {ins['pc']}: VARG with open results must be followed by TSETM")
                nxt = proto[idx + 1]
                if nxt["op"] != "TSETM" or not nxt["nums"] or nxt["nums"][0] != a:
                    raise ScanError(f"pc {ins['pc']}: VARG open-result base mismatch with following TSETM")
            else:
                hole = find_range_hole(holes, a, a + b - 2)
                if hole is not None:
                    raise ScanError(
                        f"pc {ins['pc']}: VARG destination range [{a},{a + b - 2}) "
                        f"cross FR2 hole at register {hole}"
                    )
        elif op == "TSETM":
            idx = pc_index[ins["pc"]]
            if idx == 0:
                raise ScanError(f"pc {ins['pc']}: TSETM has no preceding open-result producer")
            prev = proto[idx - 1]
            if prev["op"] not in ("CALL", "VARG") or len(prev["nums"]) < 2 or prev["nums"][1] != 0:
                raise ScanError(f"pc {ins['pc']}: TSETM must follow CALL/VARG with open results")
            if prev["nums"][0] != ins["nums"][0]:
                raise ScanError(f"pc {ins['pc']}: TSETM base mismatch with preceding {prev['op']}")
        elif op == "RET":
            a, d = nums
            hole = find_range_hole(holes, a, a + d - 2)
            if hole is not None:
                raise ScanError(
                    f"pc {ins['pc']}: RET result range [{a},{a + d - 2}) "
                    f"cross FR2 hole at register {hole}"
                )
        elif op == "CAT":
            _, b, c = nums
            hole = find_range_hole(holes, b, c)
            if hole is not None:
                raise ScanError(
                    f"pc {ins['pc']}: CAT operand range [{b},{c}) crosses FR2 hole at register {hole}"
                )
        elif op in ("FORI", "JFORI", "FORL", "IFORL", "JFORL"):
            a = nums[0]
            hole = find_range_hole(holes, a, a + 3)
            if hole is not None:
                raise ScanError(
                    f"pc {ins['pc']}: numeric for-loop control slots [{a},{a + 3}) "
                    f"cross FR2 hole at register {hole}"
                )

    return holes, max_reg


def format_context(proto: list[dict], pc: int, radius: int) -> str:
    pcs = [ins["pc"] for ins in proto]
    index = pcs.index(pc)
    lines = []
    for ins in proto[max(0, index - radius) : min(len(proto), index + radius + 1)]:
        prefix = ">>" if ins["pc"] == pc else "  "
        lines.append(f"{prefix} {ins['raw']}")
    return "\n".join(lines)


def scan_path(path: str, luajit: str, cwd: str, context: int) -> int:
    files = []
    if os.path.isdir(path):
        for name in sorted(os.listdir(path)):
            full = os.path.join(path, name)
            if os.path.isfile(full):
                files.append(full)
    else:
        files.append(path)

    focus_counts = Counter()
    ok = 0
    fail = 0

    for file_path in files:
        text = run_bl(luajit, cwd, file_path)
        protos = parse_bytecode(text)
        for proto in protos:
            for ins in proto["ins"]:
                if ins["op"] in FOCUS_OPS:
                    focus_counts[ins["op"]] += 1

        failure = None
        failure_context = None
        max_holes = 0
        max_reg = -1
        for proto_index, proto in enumerate(protos):
            prepared = None
            try:
                prepared = prepare_proto(proto["ins"])
                holes, proto_max_reg = validate_proto(prepared)
                max_holes = max(max_holes, len(holes))
                max_reg = max(max_reg, proto_max_reg)
            except ScanError as exc:
                failure = f"proto {proto_index} {proto['header']}: {exc}"
                pc_match = re.search(r"pc (\d+):", str(exc))
                if pc_match:
                    failure_context = format_context(prepared if prepared is not None else proto["ins"], int(pc_match.group(1)), context)
                break

        if failure is None:
            ok += 1
            print(f"OK   {os.path.basename(file_path)} protos={len(protos)} max_holes={max_holes} max_reg={max_reg}")
        else:
            fail += 1
            print(f"FAIL {os.path.basename(file_path)} {failure}")
            if failure_context:
                print(failure_context)

    print("\nFocus opcode totals:")
    for op in FOCUS_OPS:
        print(f"  {op}: {focus_counts[op]}")

    print(f"\nSummary: ok={ok} fail={fail}")
    return 1 if fail else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", help="Bytecode file or directory to scan")
    parser.add_argument("--luajit", default=r"D:\Games\work\tolua_runtime\luajit-2.1\src\luajit.exe")
    parser.add_argument("--cwd", default=r"D:\Games\work\tolua_runtime\luajit-2.1\src")
    parser.add_argument("--context", type=int, default=3)
    args = parser.parse_args()
    return scan_path(args.path, args.luajit, args.cwd, args.context)


if __name__ == "__main__":
    sys.exit(main())
