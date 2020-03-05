#!/usr/bin/python
"""
  (C) Copyright 2019-2020 Intel Corporation.

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
"""

import time
from ior_test_base import IorTestBase

class IorSmall(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify the drive fragmentation does free
    the space and do not lead to ENOM_SPACE.

    :avocado: recursive
    """

    def test_nvme_fragmentation(self):
        """Jira ID: DAOS-2332.

        Test Description:
            Purpose of this test is to verify there is no Fragmentation
            after doing some IO write/delete operation for ~hour.

        Use case:
        Create object with different transfer size
        wait for Aggregation to happen
        read the data again
        Delete all the pool
        Run above code in loop for some time (2 houra) and expected
        not to fail with NO ENOM SPAC.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=nvme_fragmentation
        """
        write_flags = self.params.get("write", '/run/ior/iorflags/*')
        read_flags = self.params.get("read", '/run/ior/iorflags/*')
        apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        transfer_block_size = self.params.get("transfer_block_size",
                                              '/run/ior/iorflags/*')
        obj_class = self.params.get("obj_class", '/run/ior/iorflags/*')
        #Loop through 4 times as each round takes ~30min
        for test_loop in range(4):
            self.log.info("--Test Repeat for loop %s---", test_loop)
            self.create_pool()
            self.pool.display_pool_daos_space("Pool space at the Beginning")
            container_info = {}
            # Write IOR data for different transfer size
            self.ior_cmd.flags.update(write_flags[0])
            for oclass in obj_class:
                self.ior_cmd.daos_oclass.update(oclass)
                for api in apis:
                    self.ior_cmd.api.update(api)
                    for test in transfer_block_size:
                        self.ior_cmd.transfer_size.update(test[0])
                        self.ior_cmd.block_size.update(test[1])
                        # run ior
                        self.run_ior_with_pool()
                        container_info[test[0]] = self.container.uuid
            #Wait for Aggregation to happen, right now it's 90 seconds to start
            time.sleep(100)
            # Read IOR data for different transfer size
            self.ior_cmd.flags.update(read_flags[0])
            for oclass in obj_class:
                self.ior_cmd.daos_oclass.update(oclass)
                for api in apis:
                    self.ior_cmd.api.update(api)
                    for test in transfer_block_size:
                        # update transfer and block size
                        self.ior_cmd.transfer_size.update(test[0])
                        self.ior_cmd.block_size.update(test[1])
                        # run ior
                        self.run_ior_with_pool(cont_uuid=container_info[test[0]])
            #Destroy all the container from same pool
            for key in container_info:
                self.container.uuid = container_info[key]
                self.container.destroy()
            self.pool.display_pool_daos_space("Pool space at the End")
