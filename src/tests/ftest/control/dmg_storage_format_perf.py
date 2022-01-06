#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers


class DmgStorageFormatPerfTest(TestWithServers):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Verify storage format performance

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgStorageFormatPerfTest object."""
        super().__init__(*args, **kwargs)
        self.setup_start_servers = False # Handled manually
        self.setup_start_agents = False # Not needed

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
        max_s_per_device = self.params.get("max_s_per_device", "/run/dmg_storage_format_perf/*")

        # Setup, but don't start, the servers
        self.setup_servers()

        # Constrain the format time to expected performance
        num_devices = len(self.server_managers[0].get_config_value("bdev_list"))
        max_format_time = max_s_per_device * num_devices
        self.server_managers[0].dmg_storage_format_timeout = max_format_time

        # Start the servers, which will also format
        self.start_server_managers()
