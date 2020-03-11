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
import threading
from ior_test_base import IorTestBase
from avocado.core.exceptions import TestFail

try:
    # python 3.x
    import queue as queue
except ImportError:
    # python 2.7
    import Queue as queue

class NvmeFragmentation(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify the drive fragmentation does free
    the space and do not lead to ENOM_SPACE.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super(NvmeFragmentation, self).setUp()
        self.out_queue = queue.Queue()
        self.write_flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        self.apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.transfer_block_size = self.params.get("transfer_block_size",
                                                   '/run/ior/iorflags/*')
        self.obj_class = self.params.get("obj_class", '/run/ior/iorflags/*')

    def run_ior_parallel(self, results):
        """
        IOR Thread function

        results (queue): queue for returning thread results
        """
        container_info = {}
        # Write IOR data for different transfer size
        self.ior_cmd.flags.update(self.write_flags[0])
        for oclass in self.obj_class:
            self.ior_cmd.daos_oclass.update(oclass)
            for api in self.apis:
                self.ior_cmd.api.update(api)
                for test in self.transfer_block_size:
                    self.ior_cmd.transfer_size.update(test[0])
                    self.ior_cmd.block_size.update(test[1])
                    # run ior
                    try:
                        self.run_ior_with_pool()
                    except  TestFail as error:
                        print("--- FAIL --- IOR Command Failed {}"
                              .format(error))
                        results.put("FAIL")
                    container_info[test[0]] = self.container.uuid

        #Destroy the container created by thread
        for key in container_info:
            self.container.uuid = container_info[key]
            self.container.destroy()

    def test_nvme_fragmentation(self):
        """Jira ID: DAOS-2332.

        Test Description:
            Purpose of this test is to verify there is no Fragmentation
            after doing some IO write/delete operation for ~hour.

        Use case:
        Create object with different transfer size in parallel.
        Delete the container
        Run above code in loop for some time (1 hours) and expected
        not to fail with NO ENOM SPAC.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=nvme_fragmentation
        """
        no_of_jobs = self.params.get("no_parallel_job", '/run/ior/*')
        self.create_pool()
        self.pool.display_pool_daos_space("Pool space at the Beginning")
        for test_loop in range(4):
            self.log.info("--Test Repeat for loop %s---", test_loop)
            job_list = []
            for i in range(no_of_jobs):
                job_list.append(threading.
                                Thread(target=self.run_ior_parallel,
                                       kwargs={"results":self.out_queue}))

            for i in range(no_of_jobs):
                job_list[i].start()
                time.sleep(5)

            for i in range(no_of_jobs):
                job_list[i].join()

            while not self.out_queue.empty():
                if self.out_queue.get() == "FAIL":
                    self.fail("IOR Thread FAIL")
        self.pool.display_pool_daos_space("Pool space at the End")
