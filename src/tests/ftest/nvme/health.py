#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import division

from nvme_utils import ServerFillUp, get_device_ids
from dmg_utils import DmgCommand
from exception_utils import CommandFailure

class NvmeHealth(ServerFillUp):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate NVMe health test cases
    :avocado: recursive
    """
    def test_monitor_for_large_pools(self):
        """Jira ID: DAOS-4722.

        Test Description: Test Health monitor for large number of pools.
        Use Case: This test creates many pools and verifies the following command behavior:
            dmg storage query list-pools
            dmg storage query device-health
            dmg storage scan --nvme-health

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme
        :avocado: tags=nvme_health
        """
        # pylint: disable=attribute-defined-outside-init
        # pylint: disable=too-many-branches
        max_num_pools = self.params.get("max_num_pools", '/run/pool/*')
        total_pool_percentage = self.params.get("total_pool_percentage", '/run/pool/*') / 100
        min_nvme_per_target = self.params.get("min_nvme_per_target", '/run/pool/*')
        targets_per_engine = self.server_managers[0].get_config_value("targets")

        # Calculate the space per engine based on the percentage to use
        space_per_engine = self.get_max_storage_sizes()
        scm_per_engine = int(space_per_engine[0] * total_pool_percentage)
        nvme_per_engine = int(space_per_engine[1] * total_pool_percentage)

        # Calculate the potential number of pools and use up to the max from config
        potential_num_pools = int((nvme_per_engine / (min_nvme_per_target * targets_per_engine)))
        actual_num_pools = min(max_num_pools, potential_num_pools)

        # Split available space across the number of pools to be created
        scm_per_pool = int(scm_per_engine / actual_num_pools)
        nvme_per_pool = int(nvme_per_engine / actual_num_pools)

        # Create the pools
        self.pool = []
        for _pool in range(actual_num_pools):
            self.log.info("-- Creating pool number = %s", _pool)
            self.pool.append(self.get_pool(create=False))
            self.pool[-1].scm_size.update(scm_per_pool, "scm_size")
            self.pool[-1].nvme_size.update(nvme_per_pool, "nvme_size")
            self.pool[-1].create()

        # initialize the dmg command
        self.dmg = DmgCommand(self.bin)
        self.dmg.get_params(self)
        self.dmg.insecure.update(
            self.server_managers[0].get_config_value("allow_insecure"),
            "dmg.insecure")

        # List all pools
        self.dmg.set_sub_command("storage")
        self.dmg.sub_command_class.set_sub_command("query")
        self.dmg.sub_command_class.sub_command_class.set_sub_command("list-pools")
        for host in self.hostlist_servers:
            self.dmg.hostlist = host
            try:
                result = self.dmg.run()
            except CommandFailure as error:
                self.fail("dmg command failed: {}".format(error))
            #Verify all pools UUID listed as part of query
            for pool in self.pool:
                if pool.uuid.lower() not in result.stdout_text:
                    self.fail('Pool uuid {} not found in smd query'
                              .format(pool.uuid.lower()))

        # Get the device ID from all the servers.
        device_ids = get_device_ids(self.dmg, self.hostlist_servers)

        # Get the device health
        for host, dev_list in device_ids.items():
            self.dmg.hostlist = host
            for _dev in dev_list:
                try:
                    result = self.dmg.storage_query_device_health(_dev)
                except CommandFailure as error:
                    self.fail("dmg get device states failed {}".format(error))
                if 'State:NORMAL' not in result.stdout_text:
                    self.fail("device {} on host {} is not NORMAL".format(_dev, host))

        # Get the nvme-health
        try:
            self.dmg.storage_scan_nvme_health()
        except CommandFailure as error:
            self.fail("dmg storage scan --nvme-health failed {}".format(error))
