"""Development aid: whether a time measured in the port is the same as its reference, within the plan's 3%.

  speed_check.py <reference seconds> <measured seconds> [--tolerance 0.03]
"""
from __future__ import annotations

import argparse
import sys

TOLERANCE = 0.03


def compare(reference_s: float, measured_s: float, tolerance: float = TOLERANCE) -> bool:
    if reference_s <= 0:
        raise ValueError(f"reference time must be positive: {reference_s}")
    return abs(measured_s - reference_s) <= reference_s * tolerance


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reference", type=float)
    parser.add_argument("measured", type=float)
    parser.add_argument("--tolerance", type=float, default=TOLERANCE)
    args = parser.parse_args(argv)
    passed = compare(args.reference, args.measured, args.tolerance)
    print(f"{'pass' if passed else 'FAIL'}: {args.measured:.3f} s against {args.reference:.3f} s, "
          f"{(args.measured / args.reference - 1) * 100:+.2f}%")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
