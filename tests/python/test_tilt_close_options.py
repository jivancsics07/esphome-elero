"""Tests for cover _validate_tilt_close_options validator."""

import pytest
from conftest import make_time_period
from esphome.config_validation import Invalid

from components.elero.cover import _validate_tilt_close_options


def _config(has_command_tilt_close=False, pulse_ms=0):
    c = {}
    if has_command_tilt_close:
        c["command_tilt_close"] = 0x40
    c["tilt_close_pulse_duration"] = make_time_period(pulse_ms)
    return c


def test_neither_set_passes():
    result = _validate_tilt_close_options(_config())
    assert result is not None


def test_only_command_tilt_close_passes():
    result = _validate_tilt_close_options(_config(has_command_tilt_close=True))
    assert result is not None


def test_only_pulse_duration_passes():
    result = _validate_tilt_close_options(_config(pulse_ms=1200))
    assert result is not None


def test_both_set_raises():
    with pytest.raises(Invalid, match="mutually exclusive"):
        _validate_tilt_close_options(_config(has_command_tilt_close=True, pulse_ms=1200))


def test_command_tilt_close_with_zero_pulse_passes():
    result = _validate_tilt_close_options(_config(has_command_tilt_close=True, pulse_ms=0))
    assert result is not None
