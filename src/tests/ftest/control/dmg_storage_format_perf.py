#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from control_test_base import ControlTestBase


class DmgStorageFormatPerfTest(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Verify storage format performance

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgStorageFormatPerfTest object."""
        super().__init__(*args, **kwargs)
        self.setup_start_agents = False

    def test_dmg_storage_format_perf(self):
        """JIRA ID: DAOS-4842

        Test Description: Verify storage format takes less than <max_s_per_device>.
                          For example: node1 and node2 each have 2 devices.
                                       Format should take less than 2 * <max_s_per_device>

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=control,dmg
        :avocado: tags=dmg_storage_format_perf
        """
        max_devices = self.get_max_devices()
        max_s_per_device = self.params.get("max_s_per_device", "/run/dmg_storage_format_perf/*")
        max_format_time = max_s_per_device * max_devices
        dmg = self.server_managers[0].dmg

        self.log.info("Stopping and erasing system before format")
        dmg.system_stop(force=True)
        if dmg.result.exit_status != 0:
            self.fail("Failed to stop system")

        dmg.system_erase()
        if dmg.result.exit_status != 0:
            self.fail("Failed to erase system")

        self.server_managers[0].detect_format_ready(reformat=True)

        self.log.info("Formatting storage with a timeout of %ss per device", str(max_s_per_device))
        
        dmg.storage_format(reformat=True, timeout=max_format_time)
        if dmg.result.exit_status != 0:
            self.fail("Failed to format system")

        # Make sure servers restart
        self.server_managers[0].detect_engine_start()
