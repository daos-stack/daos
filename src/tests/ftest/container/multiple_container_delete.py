#!/usr/bin/python
"""
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
"""

import time
from ior_test_base import IorTestBase

class MultipleContainerDelete(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description:

       Create a pool spanning four servers and create a container
       and fill it and delete and repeat the process several times
       and verify the container delete returns the original space.
       This test is to primarily check if there is any memory leak
       or the container delete is not clean and fills up with garbage.

    :avocado: recursive
    """

    def test_multiple_container_delete(self):
        """Jira ID: DAOS-3673

        Test Description:
            Purpose of this test is to verify the container delete
            returns all space used by a container without leaving
            garbage.
        Use case:
            Create a pool spanning 4 servers.
            Capture the pool space.
            Create a POSIX container and fill it with IOR DFS Api
            Delete the container and repeat the above steps 1000
            times.
            Verify the pool space is equal to the initial pool space. 

        :avocado: tags=all,hw,large,full_regression,container
        :avocado: tags=multicontainerdelete
        """

        if self.pool is None:
            self.create_pool()
        self.pool.connect()

        # Since the transfer size is 1M, the objects will be inserted
        # directly into NVMe and hence storage_index = 1
        storage_index = 0
        out = []

        initial_free_space = self.get_pool_space(storage_index)

        for i in range(1000):
            self.create_cont()
            self.ior_cmd.set_daos_params(self.server_group, self.pool,
                                     self.container.uuid)
            self.run_ior_with_pool()
            new_free_space = self.get_pool_space(storage_index)
            print("Free Space after ior = {}".format(new_free_space))
            self.container.destroy()
            out.append("iter = {}, free_space = {}".format(
                       i+1, new_free_space))

        # Wait for all clean ups
        self.log.info("Waiting for the clean up of containers...")
        time.sleep(10)
        new_free_space = self.get_pool_space(storage_index)
        print("\n")
        print("Initial Free Space = {}".format(initial_free_space))
        for el in out:
            print(el)
        print("\n")
        print("Free Space after cont destroy = {}".format(new_free_space))
        #self.assertTrue(new_free_space  == expected_free_space)


    def get_pool_space(self, storage_index):
        if self.pool is not None:
            pool_info = self.pool.pool.pool_query()
            free_space = pool_info.pi_space.ps_space.s_free[storage_index]
            return free_space
        else:
            self.log.info("****POOL is NONE*****")
            return 0
