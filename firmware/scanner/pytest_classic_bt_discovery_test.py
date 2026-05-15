# SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Unlicense OR CC0-1.0
# Future coverage should also assert BLE scan start, ESP-NOW readiness, command parsing, and table rendering.
import pytest
from pytest_embedded import Dut
from pytest_embedded_idf.utils import idf_parametrize


@pytest.mark.generic
@idf_parametrize('target', ['esp32'], indirect=['target'])
def test_bt_discovery(dut: Dut) -> None:
    dut.expect_exact('Discovery started.')
