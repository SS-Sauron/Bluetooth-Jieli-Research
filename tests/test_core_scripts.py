"""
Minimal smoke tests for Bluetooth‑Jieli‑Research.
Verifies that core modules import cleanly and basic logic works.
"""

import sys
import os

# Add scripts/ to path so we can import modules from subdirectories
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'scripts'))


def test_import_jl_spp_opcode_scan():
    """Ensure the main SPP opcode scanner imports without error."""
    from jl_spp import jl_spp_opcode_scan
    assert jl_spp_opcode_scan


def test_import_jl_ch10_handshake_fuzz():
    """Ensure the channel‑10 fuzzer imports cleanly."""
    from jl_spp import jl_ch10_handshake_fuzz
    assert jl_ch10_handshake_fuzz


def test_import_ble_scripts():
    """Ensure BLE scanning utilities are importable."""
    try:
        from ble import ble_scan
        assert ble_scan
    except ImportError as e:
        # bleak may be missing in some environments – that’s OK for this test
        pass


def test_import_utils():
    """Ensure utility modules load without errors."""
    from utils import helpers       # adjust if file name differs
    assert helpers


def test_prng_pattern_structure():
    """
    If the PRNG pattern script exposes a pure function (e.g., a lookup table),
    test that it behaves as expected.
    """
    from jl_spp import jl_prng_pattern
    # Replace with a real function/constant if available
    # For now, just verify the module exists
    assert jl_prng_pattern
