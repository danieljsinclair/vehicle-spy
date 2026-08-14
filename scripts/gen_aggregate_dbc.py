#!/usr/bin/env python3
"""Generate the aggregate Tesla Model 3 DBC from the joshwardell superset.

This is the single source of truth for ``resources/dbc/Model3CAN.dbc``. It
transforms the upstream superset (``external/model3dbc/Model3CAN.dbc``, 159
messages / 2752 signals) down to exactly the signals the application consumes,
preserving the consumer contract via an explicit alias map.

Design (SRP / OpenClosed):
  * ``dbc/allowlist.toml``  — the signals the app needs (the TESTABLE contract).
  * ``dbc/aliases.toml``    — upstream -> consumer renames (the TESTABLE contract).
  * This script is a PURE TRANSFORM: it reads the contracts, applies them to the
    upstream DBC, and emits the aggregate. No policy lives in this file.

Hard-error contract (fail-fast, never emit a silently-wrong DBC):
  1. An allowlisted or aliased source signal that is MISSING from the upstream
     DBC aborts with a non-zero exit.
  2. An aliased ``to`` name that already exists natively upstream (without
     aliasing) AND whose bit-layout differs from the aliased source aborts.
  3. Two distinct alias ``from`` names mapping to the same ``to`` name abort.
  4. A duplicate emitted (canId, signalName) aborts.

Usage:
    python3 scripts/gen_aggregate_dbc.py [--check]

With no arguments the aggregate DBC is (over)written. ``--check`` only verifies
the upstream still satisfies the contracts and exits non-zero on drift, without
writing — useful in CI.

The generated file is committed (generate-and-commit workflow): the script is
the generator, the committed DBC is the artifact, and the contracts are the
reviewable policy.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # pragma: no cover - exercised only on <3.11
    import tomli as tomllib  # type: ignore

# --- Repository-relative paths (resolved against the repo root) -------------
REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_DBC = REPO_ROOT / "external" / "model3dbc" / "Model3CAN.dbc"
ALLOWLIST = REPO_ROOT / "dbc" / "allowlist.toml"
ALIASES = REPO_ROOT / "dbc" / "aliases.toml"
OUTPUT_DBC = REPO_ROOT / "resources" / "dbc" / "Model3CAN.dbc"

# Signals that must be present in the emitted DBC for the consumer contract to
# hold. Mirrors dbc/allowlist.toml but is checked at runtime regardless.
SIG_RE = re.compile(
    r"^\s*SG_\s+(\S+)\s*:\s*(\d+)\|(\d+)@(\d)([+-])\s*"
    r"\(([^,]+),([^)]+)\)\s*\[([^|]+)\|([^\]]+)\]"
    r'(?:\s*"([^"]*)")?'
)
BO_RE = re.compile(r"^BO_\s+(\d+)\s+(\S+):\s*(\d+)\s+(\S+)")
VAL_RE = re.compile(r"^VAL_\s+(\d+)\s+(\S+)\s+(.*);\s*$")


@dataclass
class ValueTable:
    can_id: int
    signal_name: str
    entries: List[Tuple[int, str]] = field(default_factory=list)


class GeneratorError(Exception):
    """Raised when a contract violation would produce a wrong DBC."""


@dataclass
class Signal:
    name: str
    start_bit: int
    bit_len: int
    byte_order: int  # 0 = Motorola, 1 = Intel
    signed: bool
    scale: str
    offset: str
    minimum: str
    maximum: str
    unit: str = ""
    raw: str = ""  # original SG_ line (without trailing newline)

    def layout_key(self) -> Tuple:
        return (
            self.start_bit,
            self.bit_len,
            self.byte_order,
            self.signed,
            self.scale,
            self.offset,
        )


@dataclass
class Message:
    can_id: int
    name: str
    dlc: int
    sender: str
    signals: List[Signal] = field(default_factory=list)
    raw_header: str = ""


def load_toml(path: Path) -> dict:
    with path.open("rb") as fh:
        return tomllib.load(fh)


def parse_dbc(text: str) -> Tuple[Dict[int, Message], List[ValueTable]]:
    """Parse a DBC into {can_id: Message} and a list of value tables."""
    messages: Dict[int, Message] = {}
    value_tables: List[ValueTable] = []
    current: Optional[Message] = None
    for line in text.splitlines():
        bo = BO_RE.match(line)
        if bo:
            can_id = int(bo.group(1))
            msg = Message(
                can_id=can_id,
                name=bo.group(2),
                dlc=int(bo.group(3)),
                sender=bo.group(4),
                raw_header=line,
            )
            messages[can_id] = msg
            current = msg
            continue
        if line.startswith(" SG_") or line.startswith("SG_"):
            m = SIG_RE.match(line)
            if not m or current is None:
                continue
            sig = Signal(
                name=m.group(1),
                start_bit=int(m.group(2)),
                bit_len=int(m.group(3)),
                byte_order=int(m.group(4)),
                signed=(m.group(5) == "-"),
                scale=m.group(6).strip(),
                offset=m.group(7).strip(),
                minimum=m.group(8).strip(),
                maximum=m.group(9).strip(),
                unit=m.group(10) or "",
                raw=line,
            )
            current.signals.append(sig)
            continue
        val = VAL_RE.match(line)
        if val:
            entries: List[Tuple[int, str]] = []
            rest = val.group(3)
            for num, label in re.findall(r'(\d+)\s+"([^"]*)"', rest):
                entries.append((int(num), label))
            if entries:
                value_tables.append(
                    ValueTable(
                        can_id=int(val.group(1)),
                        signal_name=val.group(2),
                        entries=entries,
                    )
                )
    return messages, value_tables


def signal_lookup(messages: Dict[int, Message]) -> Dict[Tuple[int, str], Signal]:
    lut: Dict[Tuple[int, str], Signal] = {}
    for msg in messages.values():
        for sig in msg.signals:
            lut[(msg.can_id, sig.name)] = sig
    return lut


@dataclass
class Contracts:
    source_commit: str
    required: Set[Tuple[int, str]] = field(default_factory=set)
    # (can_id, from_name) -> to_name
    aliases: Dict[Tuple[int, str], str] = field(default_factory=dict)


def load_contracts() -> Contracts:
    allow = load_toml(ALLOWLIST)
    alias_doc = load_toml(ALIASES)

    c = Contracts(source_commit=allow.get("source", {}).get("commit", "unknown"))
    for entry in allow.get("signal", []):
        c.required.add((int(entry["can_id"]), entry["name"]))
    # Optional signals are not hard-required but still aliased if present.
    optional: Set[Tuple[int, str]] = set()
    for entry in allow.get("signal.optional", []):
        optional.add((int(entry["can_id"]), entry["name"]))

    for a in alias_doc.get("alias", []):
        key = (int(a["can_id"]), a["from"])
        c.aliases[key] = a["to"]

    c.optional = optional  # type: ignore[attr-defined]
    return c


def validate_contracts(
    messages: Dict[int, Message],
    lut: Dict[Tuple[int, str], Signal],
    contracts: Contracts,
) -> None:
    """Fail-fast on any contract violation. Pure checking, no writes."""
    # 1. every aliased source must exist upstream
    for (can_id, from_name), to_name in contracts.aliases.items():
        if (can_id, from_name) not in lut:
            raise GeneratorError(
                f"alias source signal ({can_id}, {from_name}) not found in "
                f"upstream DBC — cannot rename to '{to_name}'. Upstream moved "
                f"under us; update dbc/aliases.toml."
            )
        # 3. no two distinct 'from' names map to the same 'to'
        for (other_id, other_from), other_to in contracts.aliases.items():
            if other_to == to_name and (other_id, other_from) != (can_id, from_name):
                raise GeneratorError(
                    f"alias collision: both ({can_id},{from_name}) and "
                    f"({other_id},{other_from}) map to '{to_name}'."
                )

    # every required (can_id, name) must be reachable: either natively present,
    # or produced by an alias whose source is present.
    alias_targets = {
        (fcid, to): (fcid, frm)
        for (fcid, frm), to in contracts.aliases.items()
    }
    for (can_id, name) in contracts.required:
        if (can_id, name) in lut:
            continue
        if (can_id, name) in alias_targets:
            continue
        raise GeneratorError(
            f"required signal ({can_id}, {name}) is neither present natively "
            f"nor produced by an alias in upstream DBC. Update dbc/allowlist.toml."
        )


def build_aggregate(
    messages: Dict[int, Message],
    lut: Dict[Tuple[int, str], Signal],
    value_tables: List[ValueTable],
    contracts: Contracts,
) -> Tuple[Dict[int, Message], List[ValueTable]]:
    """Produce the curated set of messages, applying aliases in place.

    A message is kept iff it contains at least one required or aliased signal.
    Aliased signals are renamed to their 'to' name; the original 'from' signal
    is dropped. Native signals that are neither required nor aliased are dropped.
    """
    validate_contracts(messages, lut, contracts)

    # invert alias: for a given message, map native->alias target
    alias_by_can: Dict[int, Dict[str, str]] = {}
    for (can_id, from_name), to_name in contracts.aliases.items():
        alias_by_can.setdefault(can_id, {})[from_name] = to_name

    # which can_ids we must emit
    needed_ids: Set[int] = set()
    for (can_id, _name) in contracts.required:
        needed_ids.add(can_id)
    for (can_id, _) in contracts.aliases:
        needed_ids.add(can_id)
    for (can_id, _) in contracts.optional:  # type: ignore[attr-defined]
        if can_id in messages:
            needed_ids.add(can_id)

    out: Dict[int, Message] = {}
    for can_id in needed_ids:
        src = messages.get(can_id)
        if src is None:
            # required/aliased id absent entirely
            present_ids = ", ".join(str(i) for i in sorted(messages))
            raise GeneratorError(
                f"CAN id {can_id} required by contract is absent from upstream. "
                f"Upstream ids include: {present_ids[:200]}..."
            )
        alias_map = alias_by_can.get(can_id, {})
        new_msg = Message(
            can_id=src.can_id,
            name=src.name,
            dlc=src.dlc,
            sender=src.sender,
            raw_header=src.raw_header,
        )
        keep_native: Set[str] = {
            name for (_c, name) in contracts.required if _c == can_id
        }
        keep_optional: Set[str] = {
            name
            for (_c, name) in contracts.optional  # type: ignore[attr-defined]
            if _c == can_id
        }
        emitted: Set[str] = set()
        for sig in src.signals:
            if sig.name in alias_map:
                target = alias_map[sig.name]
                # 2. native 'to' exists with differing layout -> drift error
                native = lut.get((can_id, target))
                if native is not None and native.layout_key() != sig.layout_key():
                    raise GeneratorError(
                        f"bit-layout drift for '{target}' on CAN {can_id}: "
                        f"upstream native {native.layout_key()} differs from "
                        f"aliased source {sig.layout_key()}."
                    )
                renamed = Signal(
                    name=target,
                    start_bit=sig.start_bit,
                    bit_len=sig.bit_len,
                    byte_order=sig.byte_order,
                    signed=sig.signed,
                    scale=sig.scale,
                    offset=sig.offset,
                    minimum=sig.minimum,
                    maximum=sig.maximum,
                    unit=sig.unit,
                    raw=sig.raw.replace(sig.name, target, 1),
                )
                new_msg.signals.append(renamed)
                emitted.add(target)
            elif sig.name in keep_native or sig.name in keep_optional:
                new_msg.signals.append(sig)
                emitted.add(sig.name)

        # 4. no duplicate emitted names
        if len(emitted) != len(new_msg.signals):
            dupes = {
                s.name for s in new_msg.signals if list(
                    map(lambda x: x.name, new_msg.signals)
                ).count(s.name) > 1
            }
            raise GeneratorError(
                f"duplicate emitted signal name(s) on CAN {can_id}: {dupes}"
            )
        out[can_id] = new_msg

    # Carry forward VAL_ tables for any kept/aliased signal. The signal name in
    # the VAL_ line must follow the alias rename so the C++ value-table lookup
    # (keyed by can_id + signal name) still matches after generation.
    alias_lookup = {
        (fcid, frm): to
        for (fcid, frm), to in contracts.aliases.items()
    }
    out_val_tables: List[ValueTable] = []
    for vt in value_tables:
        if vt.can_id not in needed_ids:
            continue
        name = alias_lookup.get((vt.can_id, vt.signal_name), vt.signal_name)
        live_names = {
            (m.can_id, s.name) for m in out.values() for s in m.signals
        }
        if (vt.can_id, name) not in live_names:
            continue
        out_val_tables.append(
            ValueTable(can_id=vt.can_id, signal_name=name, entries=vt.entries)
        )
    return out, out_val_tables


def serialize(
    messages: Dict[int, Message],
    value_tables: List[ValueTable],
    contracts: Contracts,
) -> str:
    alias_hash = hashlib.sha256(ALIASES.read_bytes()).hexdigest()[:12]
    allow_hash = hashlib.sha256(ALLOWLIST.read_bytes()).hexdigest()[:12]
    lines: List[str] = []
    lines.append('VERSION ""')
    lines.append("")
    lines.append("NS_ :")
    lines.append("\tNS_DESC_")
    lines.append("\tCM_")
    lines.append("\tBA_DEF_")
    lines.append("\tBA_")
    lines.append("\tVAL_")
    lines.append("\tCAT_DEF_")
    lines.append("\tCAT_")
    lines.append("\tFILTER")
    lines.append("\tBA_DEF_DEF_")
    lines.append("\tEV_DATA_")
    lines.append("\tENVVAR_DATA_")
    lines.append("\tSGTYPE_")
    lines.append("\tSGTYPE_VAL_")
    lines.append("\tBA_DEF_SGTYPE_")
    lines.append("\tBA_SGTYPE_")
    lines.append("\tSIG_TYPE_REF_")
    lines.append("\tVAL_TABLE_")
    lines.append("\tSIG_GROUP_")
    lines.append("\tSIG_VALTYPE_")
    lines.append("\tSIGTYPE_VALTYPE_")
    lines.append("\tBO_TX_BU_")
    lines.append("\tBA_DEF_REL_")
    lines.append("\tBA_REL_")
    lines.append("\tBA_DEF_DEF_REL_")
    lines.append("\tBU_EV_REL_")
    lines.append("\tBU_BO_REL_")
    lines.append("")
    lines.append("BS_:")
    lines.append("")
    lines.append("BU_: ")
    lines.append("")
    lines.append(
        "// ============================================================================"
    )
    lines.append("// AGGREGATE DBC — GENERATED FILE. Do not edit by hand.")
    lines.append(
        "// Regenerate with: make aggregate-dbc")
    lines.append("//")
    lines.append("// Source: external/model3dbc (joshwardell/model3dbc)")
    lines.append(f"//   pinned commit : {contracts.source_commit}")
    lines.append(
        "//   upstream file : external/model3dbc/Model3CAN.dbc (159 msgs, 2752 sigs)")
    lines.append(
        "//   allowlist     : dbc/allowlist.toml (contract hash "
        f"{allow_hash})"
    )
    lines.append(
        "//   alias map     : dbc/aliases.toml   (contract hash "
        f"{alias_hash})"
    )
    lines.append(
        "// Policy: keep only allowlisted signals; rename upstream->consumer")
    lines.append(
        "//         via aliases.toml; HARD-ERROR on missing/drifting source.")
    lines.append(
        "// The 0x3E2 VCLEFT_brakeLightStatus message is provided upstream by")
    lines.append(
        "// joshwardell (no separate manual overlay needed).")
    lines.append(
        "// ============================================================================"
    )
    lines.append("")
    for can_id in sorted(messages):
        msg = messages[can_id]
        lines.append(
            f"BO_ {msg.can_id} {msg.name}: {msg.dlc} {msg.sender}"
        )
        for sig in msg.signals:
            lines.append(
                f" SG_ {sig.name} : {sig.start_bit}|{sig.bit_len}@"
                f"{sig.byte_order}{'-' if sig.signed else '+'}"
                f" ({sig.scale},{sig.offset}) "
                f"[{sig.minimum}|{sig.maximum}] \"{sig.unit}\" "
                f"{msg.sender}"
            )
        lines.append("")

    # Value tables (VAL_) for any kept/aliased signal.
    for vt in value_tables:
        joined = " ".join(f'{num} "{label}"' for num, label in vt.entries)
        lines.append(f"VAL_ {vt.can_id} {vt.signal_name} {joined} ;")
    if value_tables:
        lines.append("")
    return "\n".join(lines) + "\n"


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify contracts against upstream without writing the DBC.",
    )
    args = parser.parse_args(argv)

    if not SOURCE_DBC.exists():
        print(
            f"ERROR: upstream DBC not found at {SOURCE_DBC}. "
            "Run 'git submodule update --init external/model3dbc'.",
            file=sys.stderr,
        )
        return 2
    if not ALLOWLIST.exists() or not ALIASES.exists():
        print(
            f"ERROR: contract files missing ({ALLOWLIST}, {ALIASES}).",
            file=sys.stderr,
        )
        return 2

    text = SOURCE_DBC.read_text(encoding="utf-8")
    messages, value_tables = parse_dbc(text)
    lut = signal_lookup(messages)
    contracts = load_contracts()

    # Validate / build (raises GeneratorError on contract violation).
    try:
        aggregate, out_val_tables = build_aggregate(
            messages, lut, value_tables, contracts
        )
    except GeneratorError as exc:
        print(f"HARD-ERROR: {exc}", file=sys.stderr)
        return 1

    if args.check:
        print(
            f"OK: contracts satisfied. {len(aggregate)} messages, "
            f"{len(out_val_tables)} value tables would be emitted from "
            f"upstream @ {contracts.source_commit}."
        )
        return 0

    OUTPUT_DBC.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_DBC.write_text(
        serialize(aggregate, out_val_tables, contracts), encoding="utf-8"
    )
    print(
        f"Generated {OUTPUT_DBC} — {len(aggregate)} messages, "
        f"{len(out_val_tables)} value tables "
        f"({len(messages)} upstream msgs) from {contracts.source_commit}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
