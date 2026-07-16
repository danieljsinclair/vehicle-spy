#!/usr/bin/env python3
"""sonar_live.py - the SINGLE source of truth for SonarCloud numbers.

ARCHITECTURE (design-out-staleness): every scorecard output
(``make summary``, ``make coverage-summary``, ``make coverage-scorecard``)
calls this module. It fetches the LIVE SonarCloud API per run and returns
the numbers — it NEVER reads a persistent on-disk cache. That makes staleness
architecturally impossible: there is no stale state to read because there is
no persistent state. If a number is shown, it was fetched from the API in the
last few seconds.

DRY: previously three scripts each had their own SonarCloud fetch
(``coverage_block.fetch_sonar_coverage``, ``build_summary._measures_coverage``
reading a cache, ``coverage_scorecard._load_measures`` reading a cache). All
three now route through the functions here so there is ONE fetch shape, ONE
auth shape, ONE timeout, ONE error contract.

CONTRACT
    fetch_measures(project_key) -> dict | None
        {coverage, lines_to_cover, uncovered_lines, covered} live from
        /api/measures/component. None when there is no token or the fetch /
        parse fails (callers OMIT the field gracefully rather than fabricate).

    fetch_open_report(project_key) -> dict | None
        The raw /api/issues/search?statuses=OPEN response (issues list +
        impactSeverities facet), live. None on no-token / fetch failure.

    fetch_removed_report(project_key) -> dict | None
        The raw /api/issues/search?resolutions=REMOVED&facets=impactSeverities
        response, live. Used for the sonar "total = open + removed" suffix.

AUTH
    HTTP Basic with ``$SONAR_TOKEN_ES`` (falling back to ``$SONAR_TOKEN``) as
    the username and an empty password — the same auth shape the Makefile's
    ``curl -u "$TOKEN:"`` uses.
"""
from __future__ import annotations

import base64
import json
import os
import urllib.parse
import urllib.request
from typing import Optional

SONAR_HOST = 'https://sonarcloud.io'
_TIMEOUT_SECONDS = 10


def _token() -> Optional[str]:
    """Return the SonarCloud auth token from the environment, or None."""
    return os.environ.get('SONAR_TOKEN_ES') or os.environ.get('SONAR_TOKEN_US')


def _get(path: str, query: dict) -> Optional[dict]:
    """GET ``path?query`` from SonarCloud with Basic auth, return parsed JSON.

    Returns None when there is no token or the fetch / parse fails so callers
    OMIT the field gracefully (display helpers never crash).
    """
    token = _token()
    if not token:
        return None
    url = f'{SONAR_HOST}{path}?{urllib.parse.urlencode(query)}'
    req = urllib.request.Request(url)
    cred = base64.b64encode(f'{token}:'.encode()).decode()
    req.add_header('Authorization', f'Basic {cred}')
    try:
        with urllib.request.urlopen(req, timeout=_TIMEOUT_SECONDS) as resp:
            return json.loads(resp.read().decode('utf-8'))
    except (OSError, ValueError):
        return None


def fetch_measures(project_key: str) -> Optional[dict]:
    """Live GET the coverage measures for ``project_key``.

    Returns ``{coverage, lines_to_cover, uncovered_lines, covered}`` (covered
    derived as lines_to_cover - uncovered_lines so it always reconciles), or
    None when there is no token / the fetch fails / the metric is absent.
    """
    data = _get('/api/measures/component', {
        'component': project_key,
        'metricKeys': 'coverage,lines_to_cover,uncovered_lines',
    })
    if data is None:
        return None
    measures = {}
    for m in (data.get('component', {}) or {}).get('measures', []) or []:
        metric = m.get('metric')
        value = m.get('value')
        if metric and value is not None:
            measures[metric] = value
    if 'coverage' not in measures:
        return None
    try:
        coverage = float(measures['coverage'])
        ltc = int(float(measures.get('lines_to_cover', 0) or 0))
        unc = int(float(measures.get('uncovered_lines', 0) or 0))
    except (TypeError, ValueError):
        return None
    return {
        'coverage': coverage,
        'lines_to_cover': ltc,
        'uncovered_lines': unc,
        'covered': ltc - unc,
    }


def fetch_open_report(project_key: str) -> Optional[dict]:
    """Live GET the OPEN issues/search response (with the impactSeverities facet).

    Returns the raw API response (issues list + facets) so the caller can derive
    the OPEN count, blocker count, and severity breakdown the same way the
    dashboard does. None on no-token / fetch failure.
    """
    return _get('/api/issues/search', {
        'componentKeys': project_key,
        'statuses': 'OPEN',
        'ps': '500',
        'facets': 'impactSeverities',
    })


def fetch_removed_report(project_key: str) -> Optional[dict]:
    """Live GET the REMOVED issues/search response (for the total = open+removed).

    Returns the raw API response with the impactSeverities facet, or None on
    no-token / fetch failure. Used to compute the sonar ``total`` suffix.
    """
    return _get('/api/issues/search', {
        'componentKeys': project_key,
        'resolutions': 'REMOVED',
        'ps': '500',
        'facets': 'impactSeverities',
    })
