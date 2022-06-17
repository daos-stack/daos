#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import ctypes
import traceback
from multiprocessing import sharedctypes

from avocado import fail_on
from apricot import TestWithServers
from pydaos.raw import DaosPool, DaosContainer, DaosApiError, IOV


class GlobalHandle(TestWithServers):
    """Test the ability to share container handles among processes.

    :avocado: recursive
    """

    @fail_on(DaosApiError)
    def check_handle(self, pool_glob_handle, uuidstr, cont_glob_handle, rank):
        """Verify that the global handles can be turned into local handles.

        This gets run in a child process and verifies the global handles can be
        turned into local handles in another process.

        Args:
            pool_glob_handle (sharedctypes.RawValue): pool handle
            uuidstr (sharedctypes.RawArray): pool uuid
            cont_glob_handle (sharedctypes.RawValue): container handle
            rank (int): pool svc rank

        Raises:
            DaosApiError: if there was an error converting the pool handle or
                using the local pool handle to create a container.

        """
        # setup the pool and connect using global handle
        pool = DaosPool(self.context)
        pool.uuid = uuidstr
        pool.set_svc(rank)
        buf = ctypes.cast(
            pool_glob_handle.iov_buf,
            ctypes.POINTER(ctypes.c_byte * pool_glob_handle.iov_buf_len))
        buf2 = bytearray()
        buf2.extend(buf.contents)
        pool_handle = pool.global2local(
            self.context, pool_glob_handle.iov_len,
            pool_glob_handle.iov_buf_len, buf2)

        # perform an operation that will use the new handle, if it
        # doesn't throw an exception, then all is well.
        pool.pool_query()

        # setup the container and then connect using the global handle
        container = DaosContainer(self.context)
        container.poh = pool_handle
        buf = ctypes.cast(
            cont_glob_handle.iov_buf,
            ctypes.POINTER(ctypes.c_byte * cont_glob_handle.iov_buf_len))
        buf2 = bytearray()
        buf2.extend(buf.contents)
        _ = container.global2local(
            self.context, cont_glob_handle.iov_len,
            cont_glob_handle.iov_buf_len, buf2)
        # just try one thing to make sure handle is good
        container.query()

    def test_global_handle(self):
        """Test Description: Use a pool handle in another process.

        :avocado: tags=all,daily_regression
        :avocado: tags=tiny
        :avocado: tags=container,global_handle,container_global_handle
        """
        # initialize a python pool object then create the underlying
        # daos storage and connect to it
        self.add_pool(create=True, connect=True)

        # create a pool global handle
        iov_len, buf_len, buf = self.pool.pool.local2global()
        buftype = ctypes.c_byte * buf_len
        c_buf = buftype.from_buffer(buf)
        sct_pool_handle = (
            sharedctypes.RawValue(
                IOV, ctypes.cast(c_buf, ctypes.c_void_p), buf_len, iov_len))

        # create a container
        self.add_container(self.pool)
        self.container.open()

        try:
            # create a container global handle
            iov_len, buf_len, buf = self.container.container.local2global()
            buftype = ctypes.c_byte * buf_len
            c_buf = buftype.from_buffer(buf)
            sct_cont_handle = (
                sharedctypes.RawValue(
                    IOV, ctypes.cast(c_buf, ctypes.c_void_p), buf_len, iov_len))

            sct_pool_uuid = sharedctypes.RawArray(
                ctypes.c_byte, self.pool.pool.uuid)
            # this should work in the future but need on-line server addition
            # arg_list = (
            # p = Process(target=check_handle, args=arg_list)
            # p.start()
            # p.join()
            # for now verifying global handle in the same process which is not
            # the intended use case
            self.check_handle(
                sct_pool_handle, sct_pool_uuid, sct_cont_handle, 0)

        except DaosApiError as error:
            self.log.error(error)
            self.log.error(traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")
