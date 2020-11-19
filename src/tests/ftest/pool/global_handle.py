#!/usr/bin/python
'''
  (C) Copyright 2018-2020 Intel Corporation.

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
  The Governments rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function
import traceback

from apricot import TestWithServers
import check_for_pool
from pydaos.raw import DaosPool, DaosContainer, DaosApiError
from test_utils_pool import TestPool

class GlobalHandle(TestWithServers):

    """
    This class contains tests to verify the ability to share pool
    handles among processes.
    :avocado: recursive
    """

    def tearDown(self):
        try:
            super(GlobalHandle, self).tearDown()
        finally:
            # really make sure everything is gone
            check_for_pool.cleanup_pools(self.hostlist_servers)

    def check_handle(self, buf_len, iov_len, buf, uuidstr, rank):
        """
        This gets run in a child process and verifyes the global
        handle can be turned into a local handle in another process.
        """

        pool = DaosPool(self.context)
        pool.set_uuid_str(uuidstr)
        pool.set_svc(rank)
        pool.group = "daos_server"

        # note that the handle is stored inside the pool as well
        dummy_local_handle = pool.global2local(self.context, iov_len,
                                               buf_len, buf)

        # perform some operations that will use the new handle
        pool.pool_query()
        container = DaosContainer(self.context)
        container.create(pool.handle)

    def test_global_handle(self):
        """
        Test ID: DAO

        Test Description: Use a pool handle in another process.

        :avocado: tags=all,pool,pr,tiny,poolglobalhandle
        """
        # initialize a python pool object then create the underlying
        # daos storage
        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)
        self.pool.create()

        self.pool.connect()

        try:
            # create a container just to make sure handle is good
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.pool.handle)

            # create a global handle
            iov_len, buf_len, buf = self.pool.pool.local2global()

            # this should work in the future but need on-line server addition
            #arg_list = (buf_len, iov_len, buf, pool.get_uuid_str(), 0)
            #p = Process(target=check_handle, args=arg_list)
            #p.start()
            #p.join()
            # for now verifying global handle in the same process which is not
            # the intended use case
            self.check_handle(buf_len, iov_len, buf,
                              self.pool.pool.get_uuid_str(), 0)

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")
