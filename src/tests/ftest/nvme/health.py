'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import division

from avocado.core.exceptions import TestFail

from nvme_utils import ServerFillUp, get_device_ids
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
        :avocado: tags=nvme,pool
        :avocado: tags=nvme_health,test_monitor_for_large_pools
        """
        max_num_pools = self.params.get("max_num_pools", '/run/pool/*')
        total_pool_percentage = self.params.get("total_pool_percentage", '/run/pool/*') / 100
        min_nvme_per_target = self.params.get("min_nvme_per_target", '/run/pool/*')
        targets_per_engine = self.server_managers[0].get_config_value("targets")

        # Calculate the space per engine based on the percentage to use
        space_per_engine = self.server_managers[0].get_available_storage()
        scm_per_engine = int(space_per_engine['scm'] * total_pool_percentage)
        nvme_per_engine = int(space_per_engine['nvme'] * total_pool_percentage)

        # Calculate the potential number of pools and use up to the max from config
        potential_num_pools = int((nvme_per_engine / (min_nvme_per_target * targets_per_engine)))
        actual_num_pools = min(max_num_pools, potential_num_pools)

        # Split available space across the number of pools to be created
        scm_per_pool = int(scm_per_engine / actual_num_pools)
        nvme_per_pool = int(nvme_per_engine / actual_num_pools)

        # Create the pools
        pool_list = []
        for pool_num in range(actual_num_pools):
            self.log.info("-- Creating pool number = %s", pool_num)
            try:
                pool_list.append(self.get_pool(scm_size=scm_per_pool, nvme_size=nvme_per_pool))
            except TestFail as error:
                if 'DER_NOSPACE' in str(error):
                    self.log.info('-- No more storage space. Skip creating more pools.')
                    break
                raise

        # initialize the dmg command
        dmg = self.get_dmg_command()

        # List all pools
        for host in self.hostlist_servers:
            dmg.hostlist = host
            try:
                result = dmg.storage_query_list_pools()
            except CommandFailure as error:
                self.fail("dmg command failed: {}".format(error))
            # Verify all pools UUID listed as part of query
            for pool in pool_list:
                if pool.uuid.lower() not in result.stdout_text:
                    self.fail('Pool uuid {} not found in smd query'.format(pool.uuid.lower()))

        # Get the device ID from all the servers.
        device_ids = get_device_ids(dmg, self.hostlist_servers)

        # Get the device health
        for host, dev_list in device_ids.items():
            dmg.hostlist = host
            for device in dev_list:
                try:
                    result = dmg.storage_query_device_health(device)
                except CommandFailure as error:
                    self.fail("dmg get device states failed {}".format(error))
                if 'State:NORMAL' not in result.stdout_text:
                    self.fail("device {} on host {} is not NORMAL".format(device, host))

        # Get the nvme-health
        try:
            dmg.storage_scan_nvme_health()
        except CommandFailure as error:
            self.fail("dmg storage scan --nvme-health failed {}".format(error))
