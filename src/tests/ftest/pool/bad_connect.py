#!/usr/bin/python3
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback
import ctypes
from avocado.core.exceptions import TestFail
from apricot import TestWithServers


class BadConnectTest(TestWithServers):
    """
    Tests pool connect calls passing NULL and otherwise inappropriate
    parameters.  This use the python API.
    :avocado: recursive
    """
    def test_connect(self):
        """
        Pass bad parameters to pool connect

        :avocado: tags=all,full_regression
        :avocado: tags=tiny
        :avocado: tags=pool,bad_connect
        """
        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode", '/run/connecttests/connectmode/*/')
        connectmode = modelist[0]
        expected_for_param.append(modelist[1])

        setlist = self.params.get("setname",
                                  '/run/connecttests/connectsetnames/*/')
        expected_for_param.append(setlist[1])

        uuidlist = self.params.get("uuid", '/run/connecttests/UUID/*/')
        connectuuid = uuidlist[0]
        expected_for_param.append(uuidlist[1])

        # if any parameter is FAIL then the test should FAIL, in this test
        # virtually everyone should FAIL since we are testing bad parameters
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        puuid = (ctypes.c_ubyte * 16)()
        # initialize a python pool object then create the underlying
        # daos storage
        self.add_pool(connect=False)
        # save this uuid since we might trash it as part of the test
        ctypes.memmove(puuid, self.pool.pool.uuid, 16)

        # trash the UUID value in various ways
        if connectuuid == 'NULLPTR':
            self.pool.pool.uuid = None
        if connectuuid == 'JUNK':
            self.pool.pool.uuid[4] = 244

        try:
            self.pool.connect(1 << connectmode)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except TestFail as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result in ['PASS']:
                self.fail("Test was expected to pass but it failed.\n")

        # cleanup the pool
        finally:
            if self.pool is not None and self.pool.pool.attached == 1:
                # restore values in case we trashed them during test
                if self.pool.pool.uuid is None:
                    self.pool.pool.uuid = (ctypes.c_ubyte * 16)()
                ctypes.memmove(self.pool.pool.uuid, puuid, 16)
                print("pool uuid after restore {}".format(
                    self.pool.pool.get_uuid_str()))
