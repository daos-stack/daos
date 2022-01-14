#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import division
import os

from nvme_utils import ServerFillUp, get_device_ids
from dmg_utils import DmgCommand
from command_utils_base import CommandFailure

class NvmeHealth(ServerFillUp):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate NVMe health test cases
    :avocado: recursive
    """
    def test_monitor_for_large_pools(self):
        """Jira ID: DAOS-4722.

        Test Description: Test Health monitor for large number of pools.
        Use Case: This tests will create the 40 number of pools and verify the
                  dmg list-pools, device-health and nvme-health works for all
                  pools.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,ib2,nvme_health
        """
        # pylint: disable=attribute-defined-outside-init
        # pylint: disable=too-many-branches
        no_of_pools = self.params.get("number_of_pools", '/run/pool/*')
        pool_capacity = self.params.get("pool_used_percentage", '/run/pool/*')
        pool_capacity = pool_capacity / 100
        storage = self.get_max_storage_sizes()

        #Create the pool from available of storage space
        single_pool_nvme_size = int((storage[1] * pool_capacity)/no_of_pools)
        single_pool_scm_size = int((storage[0] * pool_capacity)/no_of_pools)

        self.pool = []
        # Create the Large number of pools
        for _pool in range(no_of_pools):
            self.log.info("-- Creating pool number = %s", _pool)
            self.pool.append(self.get_pool(create=False))
            self.pool[-1].scm_size.update(single_pool_scm_size, "scm_size")
            self.pool[-1].nvme_size.update(single_pool_nvme_size, "nvme_size")
            self.pool[-1].create()

        # initialize the dmg command
        self.dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        self.dmg.get_params(self)
        self.dmg.insecure.update(
            self.server_managers[0].get_config_value("allow_insecure"),
            "dmg.insecure")

        # List all pools
        self.dmg.set_sub_command("storage")
        self.dmg.sub_command_class.set_sub_command("query")
        self.dmg.sub_command_class.sub_command_class.set_sub_command(
            "list-pools")
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
        for host in device_ids:
            self.dmg.hostlist = host
            for _dev in device_ids[host]:
                try:
                    result = self.dmg.storage_query_device_health(_dev)
                except CommandFailure as error:
                    self.fail("dmg get device states failed {}".format(error))
                if 'State:NORMAL' not in result.stdout_text:
                    self.fail("device {} on host {} is not NORMAL"
                              .format(_dev, host))

        # Get the nvme-health
        try:
            self.dmg.storage_scan_nvme_health()
        except CommandFailure as error:
            self.fail("dmg storage scan --nvme-health failed {}".format(error))
