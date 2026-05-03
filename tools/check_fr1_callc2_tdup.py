#!/usr/bin/env python3
"""
Check LuaJIT bytecode for one-argument calls that must be shifted to the FR2
argument slot.

This is a narrow offline guard for shapes like:

  TGETS  A=19 B=18 ...
  TDUP   A=20 ...
  CALL   A=19 B=2 C=2

In FR1 bytecode the table literal is at CALL A+1. In FR2 bytecode the first
argument is at CALL A+2. If a TDUP table literal stays at A+1 after conversion,
the callee sees the stale frame slot, which is the List2Map/ipairs failure mode
seen in SSWS_HG battle.lua.

It also checks mirrored one-argument passthrough calls like:

  GGET   A=2 ...
  MOV    A=3 B=0
  CALL   A=2 B=4 C=2

In FR2, GGET globals, Lua table functions such as BattleUtil.GetRrevRole,
and tolua/C# helpers such as LuaTool.CreateLuaTable must use CALL A+2.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ULUA_BC_OPS = """
ISLT ISGE ISLE ISGT ISEQV ISNEV ISEQS ISNES ISEQN ISNEN ISEQP ISNEP
ISTC ISFC IST ISF MOV NOT UNM LEN
ADDVN SUBVN MULVN DIVVN MODVN ADDNV SUBNV MULNV DIVNV MODNV
ADDVV SUBVV MULVV DIVVV MODVV POW CAT KSTR KCDATA KSHORT KNUM KPRI KNIL
UGET USETV USETS USETN USETP UCLO FNEW TNEW TDUP GGET GSET TGETV TGETS TGETB
TSETV TSETS TSETB TSETM CALLM CALL CALLMT CALLT ITERC ITERN VARG ISNEXT
RETM RET RET0 RET1 FORI JFORI FORL IFORL JFORL ITERL IITERL JITERL
LOOP ILOOP JLOOP JMP FUNCF IFUNCF JFUNCF FUNCV IFUNCV JFUNCV FUNCC FUNCCW
""".split()


DEF_A_OPS = {
    "MOV",
    "NOT",
    "UNM",
    "LEN",
    "ADDVN",
    "SUBVN",
    "MULVN",
    "DIVVN",
    "MODVN",
    "ADDNV",
    "SUBNV",
    "MULNV",
    "DIVNV",
    "MODNV",
    "ADDVV",
    "SUBVV",
    "MULVV",
    "DIVVV",
    "MODVV",
    "POW",
    "CAT",
    "KSTR",
    "KCDATA",
    "KSHORT",
    "KNUM",
    "KPRI",
    "KNIL",
    "UGET",
    "FNEW",
    "TNEW",
    "TDUP",
    "GGET",
    "TGETV",
    "TGETS",
    "TGETB",
    "CALLM",
    "CALL",
    "ITERC",
    "ITERN",
    "VARG",
}


@dataclass
class Ins:
    pc: int
    line: int | None
    op: str
    a: int
    b: int
    c: int
    d: int
    raw: int


@dataclass
class Proto:
    index: int
    pflags: int
    framesize: int
    firstline: int
    numline: int
    rows: list[Ins]


def read_lj_ops(header: Path) -> list[str]:
    text = header.read_text(encoding="utf-8")
    before_enum = text.split("typedef enum", 1)[0]
    return re.findall(r"_\((\w+),", before_enum)


def read_uleb128(data: bytes, pos: int, end: int | None = None) -> tuple[int, int]:
    if end is None:
        end = len(data)
    value = 0
    shift = 0
    while True:
        if pos >= end:
            raise ValueError("truncated uleb128")
        byte = data[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value, pos
        shift += 7
        if shift > 28:
            raise ValueError("uleb128 is too large")


def read_int(data: bytes, pos: int, size: int, be: bool) -> int:
    return int.from_bytes(data[pos : pos + size], "big" if be else "little")


def fields(ins: int) -> tuple[int, int, int, int, int]:
    op = ins & 0xFF
    a = (ins >> 8) & 0xFF
    b = (ins >> 24) & 0xFF
    c = (ins >> 16) & 0xFF
    d = (ins >> 16) & 0xFFFF
    return op, a, b, c, d


def parse_chunk(path: Path, lj_ops: list[str]) -> list[Proto]:
    data = path.read_bytes()
    if len(data) < 5 or data[:3] != b"\x1bLJ":
        raise ValueError(f"not a LuaJIT bytecode file: {path}")

    version = data[3]
    if version not in {1, 2}:
        raise ValueError(f"expected LuaJIT bytecode version 1 or 2, got {version}")

    op_map = [lj_ops.index(name) for name in ULUA_BC_OPS] if version == 1 else None
    pos = 4
    flags, pos = read_uleb128(data, pos)
    be = bool(flags & 0x01)
    strip = bool(flags & 0x02)

    if not strip:
        name_len, pos = read_uleb128(data, pos)
        pos += name_len

    protos: list[Proto] = []
    proto_index = 0
    while True:
        proto_len, pos = read_uleb128(data, pos)
        if proto_len == 0:
            break

        proto_start = pos
        proto_end = proto_start + proto_len
        if proto_end > len(data):
            raise ValueError(f"proto {proto_index} exceeds file size")

        pflags = data[pos]
        framesize = data[pos + 2]
        pos += 4
        _, pos = read_uleb128(data, pos, proto_end)  # numkgc
        _, pos = read_uleb128(data, pos, proto_end)  # numkn
        numbc, pos = read_uleb128(data, pos, proto_end)

        sizedbg = 0
        firstline = 0
        numline = 0
        if not strip:
            sizedbg, pos = read_uleb128(data, pos, proto_end)
            if sizedbg:
                firstline, pos = read_uleb128(data, pos, proto_end)
                numline, pos = read_uleb128(data, pos, proto_end)

        bc_pos = pos
        lineinfo_pos = proto_end - sizedbg if sizedbg else 0
        line_unit = 0
        if sizedbg and numline:
            if numline < 256:
                line_unit = 1
            elif numline < 65536:
                line_unit = 2
            else:
                line_unit = 4

        rows: list[Ins] = []
        for pc in range(numbc):
            raw = read_int(data, bc_pos + pc * 4, 4, be)
            op_raw, a, b, c, d = fields(raw)
            op_index = op_map[op_raw] if op_map is not None and op_raw < len(op_map) else op_raw
            op = lj_ops[op_index] if 0 <= op_index < len(lj_ops) else f"OP_{op_raw}"
            line = None
            if line_unit:
                lp = lineinfo_pos + pc * line_unit
                line = firstline + read_int(data, lp, line_unit, be)
            rows.append(Ins(pc, line, op, a, b, c, d, raw))

        protos.append(Proto(proto_index, pflags, framesize, firstline, numline, rows))
        pos = proto_end
        proto_index += 1

    return protos


def call_holes(rows: list[Ins]) -> set[int]:
    return {row.a for row in rows if row.op in {"CALL", "CALLT", "ITERC"}}


def map_reg(reg: int, holes: set[int]) -> int:
    return reg + sum(1 for hole in holes if hole < reg)


def find_writer(rows: list[Ins], before_idx: int, reg: int) -> Ins | None:
    for idx in range(before_idx - 1, -1, -1):
        row = rows[idx]
        if row.a == reg and row.op in DEF_A_OPS:
            return row
    return None


def describe_writer(row: Ins | None) -> str:
    if row is None:
        return "none"
    line = row.line if row.line is not None else -1
    return f"pc={row.pc},line={line},{row.op},A={row.a},B={row.b},C={row.c},D={row.d}"


def check_file(path: Path, lj_ops: list[str], require_lines: set[int], expect_layout: str) -> int:
    version = path.read_bytes()[3]
    failures: list[str] = []
    seen_required: set[int] = set()
    tdup_hits = 0
    mov_hits = 0

    if expect_layout == "fr1" and version != 1:
        failures.append(f"{path.name}: expected FR1/source bytecode, got version {version}")
    if expect_layout == "fr2" and version != 2:
        failures.append(f"{path.name}: expected FR2/converted bytecode, got version {version}")

    for proto in parse_chunk(path, lj_ops):
        holes = call_holes(proto.rows)
        for idx, call in enumerate(proto.rows):
            if call.op != "CALL" or call.c != 2 or idx < 2:
                continue

            func = proto.rows[idx - 2]
            arg = proto.rows[idx - 1]
            line = call.line if call.line is not None else -1
            if arg.op == "TDUP" and func.op in {"TGETS", "TGETV", "TGETB"} and func.a == call.a:
                shape = f"{func.op}+TDUP+CALL(C=2)"
                expected_fr1 = call.a + 1
                expected_fr2 = call.a + 2
                tdup_hits += 1
            elif arg.op == "MOV" and func.op in {"TGETS", "TGETV", "TGETB", "UGET", "GGET"} and func.a == call.a:
                shape = f"{func.op}+MOV+CALL(C=2)"
                expected_fr1 = call.a + 1
                expected_fr2 = call.a + 2
                mov_hits += 1
            else:
                continue

            mapped_call = map_reg(call.a, holes)
            mapped_arg = map_reg(arg.a, holes)
            slot_a1_writer = find_writer(proto.rows, idx, call.a + 1)
            slot_a2_writer = find_writer(proto.rows, idx, call.a + 2)
            if line in require_lines:
                seen_required.add(line)
            prefix = f"{path.name}: proto={proto.index} pc={call.pc} line={line}"

            print(
                f"{prefix} {shape} "
                f"A{call.a}/arg=A{arg.a} mapped=A{mapped_call}/arg=A{mapped_arg} "
                f"slotA+1={describe_writer(slot_a1_writer)} "
                f"slotA+2={describe_writer(slot_a2_writer)}"
            )

            if version == 1:
                if arg.a != expected_fr1:
                    failures.append(f"{prefix}: FR1 source {arg.op} is not at CALL A+1")
            else:
                should_enforce = arg.op == "TDUP" or line in require_lines or func.op in {"GGET", "UGET"}
                if should_enforce and arg.a != expected_fr2:
                    failures.append(
                        f"{prefix}: FR2 converted {arg.op} is not at CALL A+{expected_fr2 - call.a}"
                    )

    for line in sorted(require_lines):
        if line not in seen_required:
            failures.append(f"{path.name}: required line {line} has no protected CALL(C=2) site")

    if tdup_hits == 0 and mov_hits == 0:
        failures.append(f"{path.name}: no protected one-argument CALL(C=2) sites found")

    if failures:
        print("\nFAIL:")
        for msg in failures:
            print(f"  {msg}")
        return 1

    layout = "FR1 source" if version == 1 else "FR2 converted"
    print(
        f"\nOK: {tdup_hits} TDUP and {mov_hits} MOV {layout} CALL(C=2) sites "
        "have the expected argument slot."
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bytecode", type=Path)
    parser.add_argument("--lj-bc", type=Path, default=Path("luajit-2.1/src/lj_bc.h"))
    parser.add_argument(
        "--require-line",
        type=int,
        action="append",
        default=[],
        help="Require a protected CALL(C=2) site at this source line.",
    )
    parser.add_argument(
        "--expect-layout",
        choices=("fr1", "fr2", "auto"),
        default="fr2",
        help="Expected bytecode layout. Defaults to fr2 so source files do not give false confidence.",
    )
    args = parser.parse_args()

    try:
        lj_ops = read_lj_ops(args.lj_bc)
        return check_file(args.bytecode, lj_ops, set(args.require_line), args.expect_layout)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
