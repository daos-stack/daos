#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from nvme_utils import ServerFillUp
from exception_utils import CommandFailure

class NvmeFault(ServerFillUp):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate IO works fine when NVMe fault generated
                            on single or multiple servers with single drive.
    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.capacity = self.params.get('pool_capacity', '/run/faulttests/*')
        self.no_of_servers = self.params.get('no_of_servers', '/run/faulttests/*')
        self.no_of_drives = self.params.get('no_of_drives', '/run/faulttests/*')
        self.dmg = self.get_dmg_command()

        # Set to True to generate the NVMe fault during IO
        self.set_faulty_device = True

    def test_nvme_fault(self):
        """Jira ID: DAOS-4722.

        Test Description: Test NVMe disk fault.
        Use Case: Create a large pool and start filling it up. While IO is in progress,
                  remove a single disk from a single server.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme
        :avocado: tags=nvme_fault
        """
        # Create the Pool with Maximum NVMe size
        self.create_pool_max_size(nvme=True)

        # Start the IOR Command and generate the NVMe fault.
        self.start_ior_load(operation="Auto_Write", percent=self.capacity)

        self.log.info("pool_percentage_used -- After -- %s", self.pool.pool_percentage_used())

        # Check nvme-health command works
        try:
            self.dmg.hostlist = self.hostlist_servers
            self.dmg.storage_scan_nvme_health()
        except CommandFailure as error:
            self.fail("dmg storage scan --nvme-health failed: {}".format(error))
