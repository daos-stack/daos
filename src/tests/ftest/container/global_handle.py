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

import ctypes
import traceback
from multiprocessing import sharedctypes

from avocado import fail_on
from apricot import TestWithServers
import check_for_pool
from pydaos.raw import DaosPool, DaosContainer, DaosApiError, IOV


class GlobalHandle(TestWithServers):
    """
    This class contains tests to verify the ability to share container
    handles among processes.
    :avocado: recursive
    """

    def tearDown(self):
        try:
            super(GlobalHandle, self).tearDown()
        finally:
            # really make sure everything is gone
            check_for_pool.cleanup_pools(self.hostlist_servers)

    @fail_on(DaosApiError)
    def check_handle(self, pool_glob_handle, uuidstr, cont_glob_handle, rank):
        """
        This gets run in a child process and verifyes the global
        handles can be turned into local handles in another process.
        """

        # setup the pool and connect using global handle
        pool = DaosPool(self.context)
        pool.uuid = uuidstr
        pool.set_svc(rank)
        pool.group = "daos_server"
        buf = ctypes.cast(pool_glob_handle.iov_buf,
                          ctypes.POINTER(ctypes.c_byte *
                                         pool_glob_handle.iov_buf_len))
        buf2 = bytearray()
        buf2.extend(buf.contents)
        pool_handle = pool.global2local(self.context,
                                        pool_glob_handle.iov_len,
                                        pool_glob_handle.iov_buf_len,
                                        buf2)

        # perform an operation that will use the new handle, if it
        # doesn't throw an exception, then all is well.
        pool.pool_query()

        # setup the container and then connect using the global handle
        container = DaosContainer(self.context)
        container.poh = pool_handle
        buf = ctypes.cast(cont_glob_handle.iov_buf,
                          ctypes.POINTER(ctypes.c_byte *
                                         cont_glob_handle.iov_buf_len))
        buf2 = bytearray()
        buf2.extend(buf.contents)
        dummy_cont_handle = container.global2local(
            self.context, cont_glob_handle.iov_len,
            cont_glob_handle.iov_buf_len, buf2)
        # just try one thing to make sure handle is good
        container.query()

    def test_global_handle(self):
        """
        Test ID: DAO

        Test Description: Use a pool handle in another process.

        :avocado: tags=all,container,tiny,daily_regression,conthandle
        """
        # initialize a python pool object then create the underlying
        # daos storage and connect to it
        self.prepare_pool()

        # create a pool global handle
        iov_len, buf_len, buf = self.pool.pool.local2global()
        buftype = ctypes.c_byte * buf_len
        c_buf = buftype.from_buffer(buf)
        sct_pool_handle = (
            sharedctypes.RawValue(IOV,
                                  ctypes.cast(c_buf, ctypes.c_void_p),
                                  buf_len, iov_len))

        try:
            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.pool.handle)
            self.container.open()

            # create a container global handle
            iov_len, buf_len, buf = self.container.local2global()
            buftype = ctypes.c_byte * buf_len
            c_buf = buftype.from_buffer(buf)
            sct_cont_handle = (
                sharedctypes.RawValue(IOV,
                                      ctypes.cast(c_buf, ctypes.c_void_p),
                                      buf_len, iov_len))

            sct_pool_uuid = sharedctypes.RawArray(
                ctypes.c_byte, self.pool.pool.uuid)
            # this should work in the future but need on-line server addition
            #arg_list = (
            #p = Process(target=check_handle, args=arg_list)
            #p.start()
            #p.join()
            # for now verifying global handle in the same process which is not
            # the intended use case
            self.check_handle(
                sct_pool_handle, sct_pool_uuid, sct_cont_handle, 0)

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")
