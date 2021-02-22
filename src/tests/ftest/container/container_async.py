#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback

from apricot import TestWithServers
from pydaos.raw import  DaosContainer, DaosApiError
from test_utils_container import TestContainer
from test_utils_base import CallbackHandler

RC_SUCCESS = 0


class ContainerAsync(TestWithServers):
    """Tests asynchronous container operations.

    Test create, destroy, open, close, and query.

    Negative case tests the above operations with non-existing DaosContainer.
    Not all operations are tested for the negative case.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        super(ContainerAsync, self).__init__(*args, **kwargs)
        self.container = []

    def test_createasync(self):
        """Test container create for asynchronous mode.

        Test both positive and negative cases. For negative case, RC is -1002,
        but we just check if it's something other than 0 to make the test
        robust.

        The negative case is more like a test of the API implementation rather
        than DAOS itself.

        :avocado: tags=all,small,full_regression,container,cont_create_async
        """
        self.add_pool()
        ph = self.pool.pool.handle

        cbh1 = CallbackHandler()
        cbh2 = CallbackHandler()
        self.container.append(TestContainer(pool=self.pool, cb_handler=cbh1))
        self.container.append(TestContainer(pool=self.pool))

        # We can't use TestContainer.create after the pool is destroyed, but we
        # can call DaosContainer.create to create the underlying DaosContainer,
        # so manually instantiate it and set it.
        self.container[1].container = DaosContainer(self.pool.context)

        try:
            self.container[0].create()
            self.assertEqual(
                cbh1.ret_code, RC_SUCCESS,
                "Async create failed! RC = {}".format(cbh1.ret_code))

            # Destroy pool and try to create the second container. TestContainer
            # calls wait, but we're using DaosContainer, so we need to manually
            # call it.
            self.pool.destroy(1)
            self.container[1].container.create(
                poh=ph, con_uuid=None, cb_func=cbh2.callback)
            cbh2.wait()
            self.assertTrue(
                cbh2.ret_code is not None and cbh2.ret_code != 0,
                "Async create of non-existing container succeeded!")
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_destroyasync(self):
        """Test container destroy for asynchronous mode.

        Test only positive case. We don't test negative case because the API is
        implemented so that it returns the error code before executing the
        callback function, so if we try it as it is, the test hangs.

        :avocado: tags=all,small,full_regression,container,cont_destroy_async
        """
        self.add_pool()

        cbh = CallbackHandler()
        self.container.append(TestContainer(pool=self.pool))

        # We don't need to create asynchronously, so set the CallbackHandler
        # after creating it.
        self.container[0].create()
        self.container[0].cb_handler = cbh

        try:
            self.container[0].destroy()
            self.assertEqual(
                cbh.ret_code, RC_SUCCESS,
                "Async destroy failed! RC = {}".format(cbh.ret_code))
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_openasync(self):
        """Test container open for asynchronous mode.

        Test only positive case. We don't test negative case because the API is
        implemented so that it returns the error code before executing the
        callback function, so if we try it as it is, the test hangs.

        :avocado: tags=all,small,full_regression,container,cont_open_async
        """
        self.add_pool()

        cbh = CallbackHandler()
        self.container.append(TestContainer(pool=self.pool))

        # We don't need to create asynchronously, so set the CallbackHandler
        # after creating it.
        self.container[0].create()
        self.container[0].cb_handler = cbh

        try:
            self.container[0].open()
            self.assertEqual(
                cbh.ret_code, RC_SUCCESS,
                "Async open failed! RC = {}".format(cbh.ret_code))
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_closeasync(self):
        """Test container close for asynchronous mode.

        Test both positive and negative cases.

        :avocado: tags=all,small,full_regression,container,cont_close_async
        """
        self.add_pool()

        cbh1 = CallbackHandler()
        cbh2 = CallbackHandler()
        tc1 = TestContainer(pool=self.pool)
        tc2 = TestContainer(pool=self.pool)
        self.container.append(tc1)
        self.container.append(tc2)

        # We need to open to test close.
        tc1.create()
        tc1.open()
        tc1.cb_handler = cbh1

        # We'll test to close the non-existing container, so just instantiate
        # the underlying DaosContainer and set it.
        tc2.container = DaosContainer(self.pool.context)
        tc2.cb_handler = cbh2

        try:
            tc1.close()
            self.assertEqual(
                cbh1.ret_code, RC_SUCCESS,
                "Async close failed! RC = {}".format(cbh1.ret_code))

            # If we use TestContainer, it'll call the wait for us, but we're
            # using DaosContainer, so we need to manually call it.
            tc2.container.close(cb_func=cbh2.callback)
            cbh2.wait()
            self.assertTrue(
                cbh2.ret_code is not None and cbh2.ret_code != RC_SUCCESS,
                "Async close of non-existing container succeeded! " +
                "RC = {}".format(cbh2.ret_code))
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_queryasync(self):
        """Test container query for asynchronous mode.

        Test both positive and negative cases.

        :avocado: tags=all,small,full_regression,container,cont_query_async
        """
        self.add_pool()

        cbh1 = CallbackHandler()
        cbh2 = CallbackHandler()
        tc1 = TestContainer(pool=self.pool)
        tc2 = TestContainer(pool=self.pool)
        self.container.append(tc1)
        self.container.append(tc2)

        tc1.create()
        tc1.cb_handler = cbh1

        tc2.container = DaosContainer(self.pool.context)
        tc2.cb_handler = cbh2

        try:
            tc1.get_info()
            self.assertEqual(
                cbh1.ret_code, RC_SUCCESS,
                "Async query failed! RC = {}".format(cbh1.ret_code))

            tc2.container.query(cb_func=cbh2.callback)
            cbh2.wait()
            self.assertTrue(
                cbh2.ret_code is not None and cbh2.ret_code != RC_SUCCESS,
                "Async query of non-existing container succeeded! " +
                "RC = {}".format(cbh2.ret_code))
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
