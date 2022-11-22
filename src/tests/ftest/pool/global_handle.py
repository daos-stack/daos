#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback

from apricot import TestWithServers
from pydaos.raw import DaosPool, DaosContainer, DaosApiError


class GlobalHandle(TestWithServers):
    """Test the ability to share pool handles among processes.

    :avocado: recursive
    """

    def check_handle(self, buf_len, iov_len, buf, uuidstr, rank):
        """Verify that the global handle can be turned into a local handle.

        This gets run in a child process and verifies the global handle can be
        turned into a local handle in another process.

        Args:
            buf_len (object): buffer length; 1st return value from
                DaosPool.local2global()
            iov_len (object): iov length; 2nd return value from
                DaosPool.local2global()
            buf (object): buffer; 3rd return value from DaosPool.local2global()
            uuidstr (str): pool UUID
            rank (int): pool svc rank

        Raises:
            DaosApiError: if there was an error converting the pool handle or
                using the local pool handle to create a container.

        """
        pool = DaosPool(self.context)
        pool.set_uuid_str(uuidstr)
        pool.set_svc(rank)

        # note that the handle is stored inside the pool as well
        dummy_local_handle = pool.global2local(self.context, iov_len,
                                               buf_len, buf)

        # perform some operations that will use the new handle
        pool.pool_query()
        container = DaosContainer(self.context)
        container.create(pool.handle)

    def test_global_handle(self):
        """Test ID: Jira-XXXX.

        Test Description: Use a pool handle in another process.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,global_handle
        :avocado: tags=pool_global_handle,test_global_handle
        """
        # initialize a python pool object then create the underlying
        # daos storage
        self.add_pool()

        # create a container just to make sure handle is good
        self.add_container(self.pool)

        try:
            # create a global handle
            iov_len, buf_len, buf = self.pool.pool.local2global()

            # this should work in the future but need on-line server addition
            # arg_list = (buf_len, iov_len, buf, pool.get_uuid_str(), 0)
            # p = Process(target=check_handle, args=arg_list)
            # p.start()
            # p.join()
            # for now verifying global handle in the same process which is not
            # the intended use case
            self.check_handle(
                buf_len, iov_len, buf, self.pool.pool.get_uuid_str(), 0)

        except DaosApiError as error:
            self.log.error(error)
            self.log.error(traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")
