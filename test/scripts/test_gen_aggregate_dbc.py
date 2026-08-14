#!/usr/bin/env python3
"""Tests for scripts/gen_aggregate_dbc.py — the aggregate-DBC generator.

These tests lock the GENERATOR'S contract (not the generated DBC itself):

  * Round-trip: feeding the real upstream superset through the generator
    produces a DBC that contains exactly the consumer-facing signal names the
    app binds to (DI_torqueActual, DI_gear, DI_accelPedalPos, DI_vehicleSpeed,
    SCCM_steeringAngle, VCLEFT_brakeLightStatus), with the alias renames
    applied (DIR_torqueActual -> DI_torqueActual, SteeringAngle129 ->
    SCCM_steeringAngle) and the upstream-only 'from' names GONE.

  * Hard-error: if an aliased source signal's bit-layout drifts from a native
    signal of the same 'to' name, the generator must refuse to emit (non-zero
    exit), never silently produce a wrong DBC.

The generator is exercised as a module (no subprocess), so failures point
directly at the transform logic. The real upstream superset is required
(external/model3dbc must be initialised); if it is absent the round-trip test
is skipped rather than failing the suite (CI always has the submodule).

Run:  python3 test/scripts/test_gen_aggregate_dbc.py
"""

import importlib.util
import io
import sys
import unittest
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
GEN = REPO_ROOT / "scripts" / "gen_aggregate_dbc.py"
SOURCE_DBC = REPO_ROOT / "external" / "model3dbc" / "Model3CAN.dbc"


def _load_module():
    spec = importlib.util.spec_from_file_location("gen_aggregate_dbc", GEN)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["gen_aggregate_dbc"] = mod  # required for dataclasses in 3.14
    spec.loader.exec_module(mod)
    return mod


gen = _load_module()


class GeneratorRoundTripTest(unittest.TestCase):
    """The generator emits the consumer contract from the real superset."""

    @classmethod
    def setUpClass(cls):
        if not SOURCE_DBC.exists():
            raise unittest.SkipTest(
                "upstream superset absent (git submodule update --init "
                "external/model3dbc)"
            )
        text = SOURCE_DBC.read_text(encoding="utf-8")
        cls.messages, cls.value_tables = gen.parse_dbc(text)
        cls.lut = gen.signal_lookup(cls.messages)
        cls.contracts = gen.load_contracts()
        cls.aggregate, cls.val_tables = gen.build_aggregate(
            cls.messages, cls.lut, cls.value_tables, cls.contracts
        )

    def _find_signal(self, can_id, name):
        msg = self.aggregate.get(can_id)
        if msg is None:
            return None
        for sig in msg.signals:
            if sig.name == name:
                return sig
        return None

    def test_required_consumer_signals_present(self):
        # Every signal the app binds to survives in the generated DBC.
        for can_id, name in [
            (264, "DI_torqueActual"),
            (280, "DI_accelPedalPos"),
            (280, "DI_gear"),
            (297, "SCCM_steeringAngle"),
            (599, "DI_vehicleSpeed"),
            (994, "VCLEFT_brakeLightStatus"),
        ]:
            with self.subTest(signal=f"{can_id}:{name}"):
                self.assertIsNotNone(
                    self._find_signal(can_id, name),
                    f"consumer signal {name} missing from generated DBC",
                )

    def test_alias_renames_upstream_name(self):
        # DIR_torqueActual (upstream) is renamed to DI_torqueActual.
        self.assertIsNotNone(self._find_signal(264, "DI_torqueActual"))
        # The original upstream name must NOT survive.
        self.assertIsNone(self._find_signal(264, "DIR_torqueActual"))
        # SteeringAngle129 (upstream) is renamed to SCCM_steeringAngle.
        self.assertIsNotNone(self._find_signal(297, "SCCM_steeringAngle"))
        self.assertIsNone(self._find_signal(297, "SteeringAngle129"))

    def test_alias_preserves_bit_layout(self):
        # The rename must be a pure rename: bit-layout identical to upstream.
        upstream = self.lut[(264, "DIR_torqueActual")]
        generated = self._find_signal(264, "DI_torqueActual")
        self.assertEqual(generated.layout_key(), upstream.layout_key())

        up_steer = self.lut[(297, "SteeringAngle129")]
        gen_steer = self._find_signal(297, "SCCM_steeringAngle")
        self.assertEqual(gen_steer.layout_key(), up_steer.layout_key())

    def test_gear_value_table_carried(self):
        # Gear decoding depends on the VAL_ 280 DI_gear table. It must survive.
        labels = {
            entry[1]
            for vt in self.val_tables
            if vt.can_id == 280 and vt.signal_name == "DI_gear"
            for entry in vt.entries
        }
        self.assertIn("DI_GEAR_D", labels)
        self.assertIn("DI_GEAR_P", labels)

    def test_output_is_substantially_curated(self):
        # The generator curates hard: 159 upstream messages -> a handful.
        self.assertLess(len(self.aggregate), 30)
        self.assertGreater(len(self.aggregate), 0)


class GeneratorHardErrorTest(unittest.TestCase):
    """Bit-layout drift / missing source must HARD-ERROR, never emit wrong DBC."""

    def _make_drifting_upstream(self):
        """Build an in-memory upstream where DI_gear has a drifting native twin.

        We synthesise a minimal DBC that has BOTH a native 'DI_gear' (with a
        bit-layout that differs from what the alias would produce) AND an alias
        whose 'to' collides. This forces the drift check in build_aggregate.
        """
        # Use the real parser on a hand-built DBC string.
        dbc = (
            'VERSION ""\n'
            "NS_ :\n\tNS_DESC_\n\tCM_\n\tBA_DEF_\n\tBA_\n\tVAL_\n"
            "\tCAT_DEF_\n\tCAT_\n\tFILTER\n\tBA_DEF_DEF_\n\tEV_DATA_\n"
            "\tENVVAR_DATA_\n\tSGTYPE_\n\tSGTYPE_VAL_\n\tBA_DEF_SGTYPE_\n"
            "\tBA_SGTYPE_\n\tSIG_TYPE_REF_\n\tVAL_TABLE_\n\tSIG_GROUP_\n"
            "\tSIG_VALTYPE_\n\tSIGTYPE_VALTYPE_\n\tBO_TX_BU_\n"
            "\tBA_DEF_REL_\n\tBA_REL_\n\tBA_DEF_DEF_REL_\n\tBU_EV_REL_\n"
            "\tBU_BO_REL_\n"
            "BS_:\n"
            "BU_: X\n"
            "BO_ 280 DriftMsg: 8 X\n"
            " SG_ DI_gear : 21|3@1+ (1,0) [0|7] \"\" X\n"
            " SG_ DI_gearAliasSrc : 8|4@1+ (1,0) [0|15] \"\" X\n"
        )
        messages, value_tables = gen.parse_dbc(dbc)
        return messages, value_tables

    def test_layout_drift_hard_errors(self):
        # Alias DI_gearAliasSrc -> DI_gear, but a native DI_gear already exists
        # with a different layout (8|4 vs 21|3). Generator must refuse.
        messages, value_tables = self._make_drifting_upstream()
        lut = gen.signal_lookup(messages)
        contracts = gen.Contracts(source_commit="test")
        contracts.aliases = {(280, "DI_gearAliasSrc"): "DI_gear"}
        contracts.required = set()
        contracts.optional = set()  # type: ignore[attr-defined]

        buf = io.StringIO()
        with redirect_stdout(buf), redirect_stderr(buf):
            with self.assertRaises(gen.GeneratorError):
                gen.build_aggregate(messages, lut, value_tables, contracts)

    def test_missing_alias_source_hard_errors(self):
        # Alias references a source signal absent from upstream -> refuse.
        messages, value_tables = self._make_drifting_upstream()
        lut = gen.signal_lookup(messages)
        contracts = gen.Contracts(source_commit="test")
        contracts.aliases = {(280, "NonexistentSignal"): "DI_gear"}
        contracts.required = {(280, "DI_gear")}
        contracts.optional = set()  # type: ignore[attr-defined]

        buf = io.StringIO()
        with redirect_stdout(buf), redirect_stderr(buf):
            with self.assertRaises(gen.GeneratorError):
                gen.build_aggregate(messages, lut, value_tables, contracts)


if __name__ == "__main__":
    unittest.main(verbosity=2)
