#!/usr/bin/python
'''
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
import os
from nvme_utils import ServerFillUp
from dmg_utils import DmgCommand
from command_utils_base import CommandFailure

class NvmeFault(ServerFillUp):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate IO works fine when NVMe fault generated
                            on single or multiple servers with single drive.
    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(NvmeFault, self).setUp()
        self.no_of_pools = self.params.get("number_of_pools", '/run/pool/*', 1)
        self.capacity = self.params.get("percentage",
                                        '/run/faulttests/pool_capacity/*')
        self.no_of_servers = self.params.get("count",
                                             '/run/faulttests/no_of_servers/*/')
        self.no_of_drives = self.params.get("count",
                                            '/run/faulttests/no_of_drives/*/')
        self.dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        self.dmg.get_params(self)
        self.dmg.insecure.update(
            self.server_managers[0].get_config_value("allow_insecure"),
            "dmg.insecure")
        #Set to True to generate the NVMe fault during IO
        self.set_faulty_device = True

    def test_nvme_fault(self):
        """Jira ID: DAOS-4722.

        Test Description: Test NVMe disk fault.
        Use Case: Create the large size of pool and start filling up the pool.
                  while IO is in progress remove single disks from
                  single/multiple servers.

        :avocado: tags=all,hw,medium,nvme,ib2,nvme_fault,full_regression
        """
        #Create the Pool with Maximum NVMe size
        self.create_pool_max_size(nvme=True)

        #Start the IOR Command and generate the NVMe fault.
        self.start_ior_load(percent=self.capacity)

        print("pool_percentage_used -- After -- {}"
              .format(self.pool.pool_percentage_used()))

        #Check nvme-health command works
        try:
            self.dmg.hostlist = self.hostlist_servers
            self.dmg.storage_scan_nvme_health()
        except CommandFailure as _error:
            self.fail("dmg storage scan --nvme-health failed")
