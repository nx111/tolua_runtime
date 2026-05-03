#!/usr/bin/env python3
"""
Check FR2 call-frame-shift rewrites.

When a CALL base is moved right to avoid overlapping live registers, FR2 still
requires a gap after the function slot:

  old:  CALL A14 C=4, args A15,A16,A17
  new:  CALL A15 C=4, args A17,A18,A19

The broken form moved the function to A15 but kept args at A16,A17,A18, which
made methods such as BattleUtil.FilterTalents receive shifted arguments.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_fr1_callc2_tdup import Ins, Proto, parse_chunk, read_lj_ops  # noqa: E402


def last_writer(rows: list[Ins], before_idx: int, reg: int) -> Ins | None:
    for idx in range(before_idx - 1, -1, -1):
        row = rows[idx]
        if row.a == reg:
            return row
    return None


def describe(row: Ins | None) -> str:
    if row is None:
        return "none"
    line = row.line if row.line is not None else -1
    return f"pc={row.pc},line={line},{row.op},A={row.a},C={row.c}"


def check_proto(proto: Proto, require_lines: set[int], failures: list[str]) -> set[int]:
    seen_required: set[int] = set()
    for idx, call in enumerate(proto.rows):
        if call.op != "CALL" or call.c < 3:
            continue

        line = call.line if call.line is not None else -1
        nargs = call.c - 1
        func_writer = last_writer(proto.rows, idx, call.a)
        if not (
            func_writer
            and func_writer.pc == call.pc - 1
            and func_writer.op == "MOV"
            and func_writer.c == call.a - 1
        ):
            continue

        if line in require_lines:
            seen_required.add(line)

        bad_parts: list[str] = []
        for arg_index in range(nargs):
            dst = call.a + 2 + arg_index
            src = call.a + arg_index
            writer = last_writer(proto.rows, idx, dst)
            if not (writer and writer.op == "MOV" and writer.c == src):
                bad_parts.append(f"arg{arg_index + 1}=A{dst} expected MOV from A{src}, got {describe(writer)}")

        if bad_parts:
            failures.append(
                f"proto={proto.index} pc={call.pc} line={line} CALL A={call.a} C={call.c}: "
                + "; ".join(bad_parts)
            )
        else:
            print(
                f"OK proto={proto.index} pc={call.pc} line={line} "
                f"CALL A={call.a} C={call.c} args=A{call.a + 2}..A{call.a + nargs + 1}"
            )

    return seen_required


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bytecode", type=Path)
    parser.add_argument("--lj-bc", type=Path, default=Path("luajit-2.1/src/lj_bc.h"))
    parser.add_argument("--require-line", type=int, action="append", default=[])
    args = parser.parse_args()

    failures: list[str] = []
    required = set(args.require_line)
    seen: set[int] = set()
    lj_ops = read_lj_ops(args.lj_bc)

    if args.bytecode.read_bytes()[3] != 2:
        failures.append(f"{args.bytecode.name}: expected FR2 bytecode")

    for proto in parse_chunk(args.bytecode, lj_ops):
        seen.update(check_proto(proto, required, failures))

    for line in sorted(required - seen):
        failures.append(f"{args.bytecode.name}: required frame-shift CALL line {line} not found")

    if failures:
        print("\nFAIL:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
