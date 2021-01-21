#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import print_function

import traceback

from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError


class SameKeyDifferentValue(TestWithServers):
    """
    Test Description: Test to verify different type of values
    passed to same akey and dkey.
    :avocado: recursive
    """
    def setUp(self):
        super(SameKeyDifferentValue, self).setUp()
        self.prepare_pool()

        try:
            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.pool.handle)

            # now open it
            self.container.open()

        except DaosApiError as excpn:
            print(excpn)
            print(traceback.format_exc())
            self.fail("Test failed during setup.\n")

    def test_single_to_array_value(self):
        """
        Jira ID: DAOS-2218
        Test Description: Test to verify different type of
        values passed (i.e. single to array value) to the same akey and dkey.
        Case1: Insert akey,dkey with single value
               Insert same akey,dkey with array value
               Result: should return -1001 ERR.
        Case2: Insert akey,dkey with single value
               Punch the keys
               Insert same akey,dkey under same object with array value
               Result: should either pass or return -1001 ERR
        Case3: Insert akey,dkey with single value
               Punch the keys
               Trigger aggregation
               Insert same akey,dkey under same object with array value
               Result: should either pass or return -1001 ERR
        :avocado: tags=object,samekeydifferentvalue,singletoarray,vm,small
        """

        # define akey,dkey, single value data and array value data
        single_value_data = "a string that I want to stuff into an object"
        array_value_data = []
        array_value_data.append("data string one")
        array_value_data.append("data string two")
        array_value_data.append("data string tre")

        dkey = "this is the dkey"
        akey = "this is the akey"

        aggregation = False

        for i in range(3):
            try:
                # create an object and write single value data into it
                obj = self.container.write_an_obj(single_value_data,
                                                  len(single_value_data)+1,
                                                  dkey, akey, obj_cls=1)

                # read the data back and make sure its correct
                read_back_data = self.container.read_an_obj(
                    len(single_value_data)+1, dkey, akey, obj)
                if single_value_data != read_back_data.value:
                    print("data I wrote:" + single_value_data)
                    print("data I read back" + read_back_data.value)
                    self.fail("Write data, read it back, didn't match\n")

                # test case 1
                if i == 0:
                    try:
                        # write array value data to same keys, expected to fail
                        self.container.write_an_array_value(array_value_data,
                                                            dkey, akey, obj,
                                                            obj_cls=1)

                        # above line is expected to return an error,
                        # if not fail the test
                        self.fail("Array value write to existing single value"
                                  + " key should have failed\n")

                    # should fail with -1001 ERR
                    except DaosApiError as excp:
                        if "-1001" not in str(excp):
                            print(excp)
                            self.fail("Should have failed with -1001 error"
                                      + " message, but it did not\n")

                # test case 2 and 3
                elif i == 1 or 2:
                    try:
                        # punch the keys
                        obj.punch_akeys(0, dkey, [akey])
                        obj.punch_dkeys(0, [dkey])

                        if aggregation is True:
                            # trigger aggregation
                            self.container.aggregate(self.container.coh, 0)

                        # write to the same set of keys under same object
                        # with array value type
                        self.container.write_an_array_value(array_value_data,
                                                            dkey, akey, obj,
                                                            obj_cls=1)

                    # above write of array value should either succeed
                    # or fail with -1001 ERR
                    except DaosApiError as excp:
                        if "-1001" not in str(excp):
                            print(excp)
                            self.fail("Should have failed with -1001 error"
                                      + " message or the write should have"
                                      + " been successful, but it did not\n")

                    # change the value of aggregation to test Test Case 3
                    aggregation = True

                # punch the entire object after each iteration
                obj.close()

            # catch the exception if test fails to write to an object
            # or fails to punch the written object
            except DaosApiError as excp:
                self.fail("Failed to write to akey/dkey or punch the object")

    def test_array_to_single_value(self):
        """
        Jira ID: DAOS-2218
        Test Description: Test to verify different type of
        values passed (i.e array to single value) to the same akey and dkey.
        Case1: Insert akey,dkey with array value
               Insert same akey,dkey with single value
               Result: should return -1001 ERR.
        Case2: Insert akey,dkey with array value
               Punch the keys
               Insert same akey,dkey under same object with single value
               Result: should either pass or return -1001 ERR
        Case3: Insert akey,dkey with array value
               Punch the keys
               Trigger aggregation
               Insert same akey,dkey under same object with single value
               Result: should either pass or return -1001 ERR
        :avocado: tags=object,samekeydifferentvalue,arraytosingle,vm,small
        """

        # define akey,dkey, single value data and array value data
        single_value_data = "a string that I want to stuff into an object"
        array_value_data = []
        array_value_data.append("data string one")
        array_value_data.append("data string two")
        array_value_data.append("data string tre")

        dkey = "this is the dkey"
        akey = "this is the akey"

        aggregation = False

        for i in range(3):
            try:
                # create an object and write array value data into it
                obj = self.container.write_an_array_value(array_value_data,
                                                          dkey, akey,
                                                          obj_cls=1)
                # read the data back and make sure its correct
                length = len(array_value_data[0])
                read_back_data = self.container.read_an_array(
                    len(array_value_data), length + 1, dkey, akey, obj)

                for j in range(3):
                    if (array_value_data[j][0:length-1] !=
                            read_back_data[j][0:length-1]):
                        print("Written Data: {}".format(array_value_data[j]))
                        print("Read Data: {}".format(read_back_data[j]))
                        self.fail("Data mismatch\n")
                # test case 1
                if i == 0:
                    try:
                        # write single value data to same keys, expected to fail
                        self.container.write_an_obj(single_value_data,
                                                    len(single_value_data)+1,
                                                    dkey, akey, obj, obj_cls=1)
                        # above line is expected to return an error,
                        # if not fail the test
                        self.fail("Single value write to existing array value"
                                  + " key should have failed\n")

                    # should fail with -1001 ERR
                    except DaosApiError as excp:
                        if "-1001" not in str(excp):
                            print(excp)
                            self.fail("Should have failed with -1001 error"
                                      + " message, but it did not\n")

                # test case 2 and 3
                elif i == 1 or 2:
                    try:
                        # punch the keys
                        obj.punch_akeys(0, dkey, [akey])
                        obj.punch_dkeys(0, [dkey])

                        if aggregation is True:
                            # trigger aggregation
                            self.container.aggregate(self.container.coh, 0)

                        # write to the same set of keys under same object
                        # with single value type
                        self.container.write_an_obj(single_value_data,
                                                    len(single_value_data)+1,
                                                    dkey, akey, obj, obj_cls=1)
                    # above write of array value should either succeed
                    # or fail with -1001 ERR
                    except DaosApiError as excp:
                        if "-1001" not in str(excp):
                            print(excp)
                            self.fail("Should have failed with -1001 error"
                                      + " message or the write should have"
                                      + " been successful, but it did not\n")

                    # change the value of aggregation to test Test Case 3
                    aggregation = True

                # punch the entire object after each iteration
                obj.close()

            # catch the exception if test fails to write to an object
            # or fails to punch the written object
            except DaosApiError as excp:
                self.fail("Failed to write to akey/dkey or punch the object")
