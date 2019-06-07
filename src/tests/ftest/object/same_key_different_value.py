#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

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
from __future__ import print_function

import os
import time
import traceback
from apricot import TestWithServers

from daos_api import DaosPool, DaosContainer, DaosApiError

class SameKeyDifferentValue(TestWithServers):
    """
    Test Description: Test to verify different type of values
    passed to same akey and dkey.
    :avocado: recursive
    """
    def setUp(self):
        try:
            super(SameKeyDifferentValue, self).setUp()

            # parameters used in pool create
            createmode = self.params.get("mode", '/run/pool/createmode/')
            createsetid = self.params.get("setname", '/run/pool/createset/')
            createsize = self.params.get("size", '/run/pool/createsize/')
            createuid = os.geteuid()
            creategid = os.getegid()

            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)
            self.pool.connect(1 << 1)

            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.handle)

            # now open it
            self.container.open()

        except DaosApiError as excpn:
            print(excpn)
            print(traceback.format_exc())
            self.fail("Test failed during setup.\n")

    def tearDown(self):

        try:
            if self.container:
                self.container.close()

            # wait a few seconds and then destroy
            time.sleep(5)
            if self.container:
                self.container.destroy()

            # cleanup the pool
            if self.pool:
                self.pool.disconnect()
                self.pool.destroy(1)

        except DaosApiError as excpn:
            print(excpn)
            print(traceback.format_exc())
            self.fail("Test failed during teardown.\n")

        finally:
            super(SameKeyDifferentValue, self).tearDown()

    def test_samekey_differentvalue(self):
        """
        Jira ID: DAOS-2218
        Test Description: Test to verify different type of
        values passed to the same akey and dkey.
        Case1: Insert akey,dkey with single value
               Insert same akey,dkey with array value
               Result: should return -1001 Error.
        Case2: Insert akey,dkey with single value
               Punch the keys
               Trigger aggregation
               Insert same akey,dkey under same object with array value
               Result: Should work without errors.
        :avocado: tags=object,samekeydifferentvalue,vm,small
        """

        try:
            # define akey,dkey, single value data and array value data
            single_value_data = "a string that I want to stuff into an object"
            array_value_data = []
            array_value_data.append("data string one")
            array_value_data.append("data string two")
            array_value_data.append("data string tre")

            dkey = "this is the dkey"
            akey = "this is the akey"

            # create an object and write single value data into it
            obj, txn = self.container.write_an_obj(single_value_data,
                                                   len(single_value_data)+1,
                                                   dkey, akey, obj_cls=1)

            # read the data back and make sure its correct
            read_back_data = self.container.read_an_obj(
                len(single_value_data)+1, dkey, akey, obj, txn)
            if single_value_data != read_back_data.value:
                print("data I wrote:" + single_value_data)
                print("data I read back" + read_back_data.value)
                self.fail("Write data, read it back, didn't match\n")

            # write array value data to same keys, expected to fail
            self.container.write_an_array_value(array_value_data,
                                                dkey, akey, obj,
                                                obj_cls=1)

            # above line is expected to return an error, if not fail the test
            self.fail("Array value write to existing single value key"
                      + " should have failed\n")
        except DaosApiError as excp:
            if "-1001" not in str(excp):
                print(excp)
                self.fail("Should have failed with -1001 error message,"
                          + " but it did not\n")
        try:
            # punch akey and dkey
            obj.punch_akeys(0, dkey, [akey])
            obj.punch_dkeys(0, [dkey])

            # trigger aggregation
            self.container.aggregate(self.container.coh, 0)

            # write array value to same set of keys under same object
            # it should be successful this time
            obj, txn = self.container.write_an_array_value(array_value_data,
                                                           dkey, akey, obj,
                                                           obj_cls=1)
        # test should fail if exception occurs
        except DaosApiError as excp:
            self.fail("Test failed while trying to write array value:{}"
                      .format(excp))
