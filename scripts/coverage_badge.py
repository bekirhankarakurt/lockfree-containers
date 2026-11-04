#!/usr/bin/env python3
"""Turn a gcovr --json-summary file into a shields.io endpoint badge JSON."""
import json
import sys


def color_for(percent: float) -> str:
    if percent >= 90:
        return "brightgreen"
    if percent >= 80:
        return "green"
    if percent >= 70:
        return "yellowgreen"
    if percent >= 60:
        return "yellow"
    return "red"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: coverage_badge.py <gcovr-summary.json> <badge.json>", file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as f:
        percent = float(json.load(f)["line_percent"])
    badge = {
        "schemaVersion": 1,
        "label": "coverage",
        "message": f"{percent:.1f}%",
        "color": color_for(percent),
    }
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        json.dump(badge, f)
    print(f"line coverage: {percent:.1f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
