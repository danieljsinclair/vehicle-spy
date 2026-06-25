#!/usr/bin/env python3
"""Display a SonarCloud issue summary for the ESP32 firmware project.

Reads a cached ``/api/issues/search`` report (JSON) and prints OPEN issue
counts by severity plus the top rules. Mirrors the severity taxonomy the
SonarCloud dashboard uses (impactSeverities facet: BLOCKER/HIGH/MEDIUM/LOW/INFO).

The report is fetched by the Makefile (``make sonar-summary`` / ``sonar-scan``)
with ``statuses=OPEN&facets=impactSeverities`` and cached to
``build-firmware/sonar-report.json``.

Usage:
    sonar_summary.py <report.json> [--label "[vehicle-sim-esp32]"] [--top N]
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from typing import Iterable

# Dashboard severity order (highest impact first).
SEVERITY_ORDER = ["BLOCKER", "HIGH", "MEDIUM", "LOW", "INFO"]

# ANSI colours (match the Makefile palette).
RED = "\033[0;31m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
NC = "\033[0m"


def severity_facet(data: dict) -> dict:
    """Read the impactSeverities facet straight from the API response.

    This is the SAME server-side counting the dashboard widget uses, so the
    local buckets always equal the dashboard rather than being re-derived.
    """
    for facet in data.get("facets") or []:
        if facet.get("property") == "impactSeverities":
            return {v["val"]: v.get("count", 0) for v in facet.get("values", [])}
    return {}


def count_by_severity(issues: Iterable[dict]) -> dict:
    """Fallback: derive severity counts per-issue if no facet is present."""
    counts: Counter = Counter()
    for issue in issues:
        # An issue can have multiple impacts; take the highest-severity one.
        impacts = issue.get("impacts") or []
        if not impacts:
            continue
        best = max(impacts, key=lambda i: SEVERITY_ORDER.index(i.get("severity", "INFO"))
                   if i.get("severity") in SEVERITY_ORDER else len(SEVERITY_ORDER))
        counts[best.get("severity", "INFO")] += 1
    return dict(counts)


def top_rules(issues: Iterable[dict], n: int) -> list:
    """Return the (rule, count, severity) tuples with the most issues."""
    rule_counts: Counter = Counter()
    rule_severity: dict = {}
    for issue in issues:
        rule = issue.get("rule", "?")
        rule_counts[rule] += 1
        impacts = issue.get("impacts") or []
        sev = (impacts[0].get("severity", "INFO") if impacts
               else issue.get("severity", "INFO"))
        if sev in SEVERITY_ORDER and (rule not in rule_severity
                                      or SEVERITY_ORDER.index(sev) < SEVERITY_ORDER.index(rule_severity[rule])):
            rule_severity[rule] = sev
    return [(rule, cnt, rule_severity.get(rule, "INFO"))
            for rule, cnt in rule_counts.most_common(n)]


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", help="path to cached issues/search JSON")
    parser.add_argument("--label", default="[vehicle-sim-esp32]",
                        help="line prefix label")
    parser.add_argument("--top", type=int, default=8,
                        help="number of top rules to show")
    args = parser.parse_args(argv)

    try:
        data = json.load(open(args.report))
    except FileNotFoundError:
        print(f"  {args.label} no cached report at {args.report}")
        return 0
    except json.JSONDecodeError as exc:
        print(f"  {args.label} invalid JSON in {args.report}: {exc}")
        return 1

    issues = data.get("issues", [])
    total = data.get("total", len(issues))

    # Prefer the server-side facet; fall back to per-issue counting.
    counts = severity_facet(data)
    if not counts and issues:
        counts = count_by_severity(issues)

    print(f"  {args.label} SonarCloud: {total} open issues")
    for sev in SEVERITY_ORDER:
        if sev in counts and counts[sev]:
            print(f"    {sev:<8} {counts[sev]}")
    if not counts:
        print("    (no severity facet in report)")

    rules = top_rules(issues, args.top)
    if rules:
        print(f"  {args.label} top rules:")
        for rule, cnt, sev in rules:
            colour = RED if sev in ("BLOCKER", "HIGH") else (YELLOW if sev == "MEDIUM" else "")
            print(f"    {colour}{sev:<8}{NC} {cnt:>3}x  {rule}")

    print(f"  {args.label} dashboard: https://sonarcloud.io/dashboard?id=danieljsinclair_vehicle-sim-esp32")
    return 0


if __name__ == "__main__":
    sys.exit(main())
