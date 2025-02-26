'''
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import division

from avocado import fail_on
from avocado.core.exceptions import TestFail
from dmg_utils import get_dmg_smd_info, get_storage_query_pool_info
from exception_utils import CommandFailure
from nvme_utils import ServerFillUp, get_device_ids


class NvmeHealth(ServerFillUp):
    """
    Test Class Description: To validate NVMe health test cases
    :avocado: recursive
    """

    @fail_on(CommandFailure)
    def test_monitor_for_large_pools(self):
        # pylint: disable=too-many-locals
        """Jira ID: DAOS-4722.

        Test Description: Test Health monitor for large number of pools.
        Use Case: This test creates many pools and verifies the following command behavior:
            dmg storage query list-pools
            dmg storage query list-devices --health
            dmg storage scan --nvme-health

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,pool
        :avocado: tags=NvmeHealth,test_monitor_for_large_pools
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

        # consider 1GiB RDB memory consume for MD-on-SSD
        rdb_size = 1073741824
        if self.server_managers[0].manager.job.using_control_metadata:
            min_scm_per_pool = 104857600
            potential_num_pools = int(scm_per_engine / (min_scm_per_pool + rdb_size))
            actual_num_pools = min(potential_num_pools, actual_num_pools)

        # Split available space across the number of pools to be created
        scm_per_pool = int(scm_per_engine / actual_num_pools)
        if self.server_managers[0].manager.job.using_control_metadata:
            scm_per_pool = int(scm_per_pool - rdb_size)
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

        # Expect each pool uuid to appear on each rank
        expected_uuids = {}
        for rank in self.server_managers[0].ranks.keys():
            expected_uuids[rank] = [pool.uuid.lower() for pool in pool_list]

        # List all pools
        errors = 0
        for host in self.server_managers[0].hosts:
            dmg.hostlist = host
            pool_info = get_storage_query_pool_info(dmg)
            self.log.info('Pools found on %s', host)
            for pool in pool_info:
                try:
                    try:
                        # Remove each uuid/rank combination found in the expected list
                        uuid_index = expected_uuids[pool['rank']].index(pool['uuid'])
                        expected_uuids[pool['rank']].pop(uuid_index)
                        error_msg = ''
                    except ValueError:
                        error_msg = ' <== ERROR: DOES NOT MATCH A CREATED POOL ON RANK {}'.format(
                            pool['rank'])
                        errors += 1
                    self.log.info('  uuid=%s, rank=%s%s', pool['uuid'], pool['rank'], error_msg)
                except KeyError as error:
                    self.fail(
                        'Error parsing dmg.storage_query_list_pools() output: {}'.format(error))

        # If each pool was found on each rank expected_uuids should be empty
        for rank in self.server_managers[0].ranks.keys():
            if expected_uuids[rank]:
                self.log.info('Pools not found on rank %s', rank)
                for uuid in expected_uuids[rank]:
                    self.log.info('  %s', uuid)
                    errors += 1
        if errors:
            self.fail(
                'Detected {} error(s) verifying dmg storage query list-pools output'.format(errors))

        # Get the device ID from all the servers.
        try:
            device_ids = get_device_ids(dmg, self.hostlist_servers)
        except CommandFailure as error:
            self.fail(str(error))

        # Get the device health
        errors = 0
        for host, uuid_dict in device_ids.items():   # pylint: disable=too-many-nested-blocks
            for uuid in sorted(uuid_dict.keys()):
                dmg.hostlist = host
                try:
                    info = get_dmg_smd_info(dmg.storage_query_list_devices, 'devices', uuid=uuid,
                                            health=True)
                except CommandFailure as error:
                    self.fail(str(error))
                self.log.info('Verifying the health of devices on %s', host)
                for devices in info.values():
                    for device in devices:
                        try:
                            error_msg = ''
                            if device['uuid'] != uuid:
                                error_msg = '  <== ERROR: UNEXPECTED DEVICE UUID'
                                errors += 1
                            elif device['ctrlr']['dev_state'].lower() != 'normal':
                                error_msg = '  <== ERROR: STATE NOT NORMAL'
                                errors += 1
                            self.log.info(
                                '  health is %s for %s%s',
                                device['ctrlr']['dev_state'], device['uuid'], error_msg)
                        except KeyError as error:
                            self.fail(
                                "Error parsing dmg.storage_query_list_devices() output: {}".format(
                                    error))
        if errors:
            self.fail(
                'Detected {} error(s) verifying dmg storage query list-devices --health output'.
                format(errors))

        # Get the nvme-health
        try:
            dmg.hostlist = self.server_managers[0].hosts
            dmg.storage_scan_nvme_health()
        except CommandFailure as error:
            self.fail("dmg storage scan --nvme-health failed {}".format(error))
