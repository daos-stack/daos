'''
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers
from pydaos.raw import DaosApiError


class SameKeyDifferentValue(TestWithServers):
    """
    Test Description: Test to verify different type of values
    passed to same akey and dkey.
    :avocado: recursive
    """

    def setUp(self):
        super().setUp()
        self.pool = self.get_pool()
        self.container = self.get_container(self.pool)
        self.container.open()

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
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=SameKeyDifferentValue,test_single_to_array_value
        """

        # define akey,dkey, single value data and array value data
        single_value_data = b"a string that I want to stuff into an object"
        array_value_data = []
        array_value_data.append(b"data string one")
        array_value_data.append(b"data string two")
        array_value_data.append(b"data string tre")

        dkey = b"this is the dkey"
        akey = b"this is the akey"

        aggregation = False

        for index in range(3):
            try:
                # create an object and write single value data into it
                obj = self.container.container.write_an_obj(
                    single_value_data, len(single_value_data) + 1, dkey, akey, obj_cls=1)

                # read the data back and make sure its correct
                read_back_data = self.container.container.read_an_obj(
                    len(single_value_data) + 1, dkey, akey, obj)
                if single_value_data != read_back_data.value:
                    self.log.info("wrote data: %s", single_value_data)
                    self.log.info("read data:  %s", read_back_data.value)
                    self.fail("Write data, read it back, didn't match\n")

                # test case 1
                if index == 0:
                    try:
                        # write array value data to same keys, expected to fail
                        self.container.container.write_an_array_value(
                            array_value_data, dkey, akey, obj, obj_cls=1)

                        # above line is expected to return an error,
                        # if not fail the test
                        self.fail(
                            "Array value write to existing single value key should have failed")

                    # should fail with -1001 ERR
                    except DaosApiError as error:
                        if "-1001" not in str(error):
                            self.log.error(error)
                            self.fail("Expected write to fail with -1001")

                # test case 2 and 3
                else:
                    try:
                        # punch the keys
                        obj.punch_akeys(0, dkey, [akey])
                        obj.punch_dkeys(0, [dkey])

                        if aggregation:
                            # trigger aggregation
                            self.container.container.aggregate(self.container.container.coh, 0)

                        # write to the same set of keys under same object with array value type
                        self.container.container.write_an_array_value(
                            array_value_data, dkey, akey, obj, obj_cls=1)

                    # above write of array value should either succeed or fail with -1001 ERR
                    except DaosApiError as error:
                        if "-1001" not in str(error):
                            self.log.error(error)
                            self.fail("Expected write to either succeed or fail with -1001")

                    # change the value of aggregation to test Test Case 3
                    aggregation = True

                # punch the entire object after each iteration
                obj.close()

            # catch the exception if test fails to write to an object
            # or fails to punch the written object
            except DaosApiError as error:
                self.log.error(error)
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
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=SameKeyDifferentValue,test_array_to_single_value
        """

        # define akey,dkey, single value data and array value data
        single_value_data = b"a string that I want to stuff into an object"
        array_value_data = []
        array_value_data.append(b"data string one")
        array_value_data.append(b"data string two")
        array_value_data.append(b"data string tre")

        dkey = b"this is the dkey"
        akey = b"this is the akey"

        aggregation = False

        for index in range(3):
            try:
                # create an object and write array value data into it
                obj = self.container.container.write_an_array_value(
                    array_value_data, dkey, akey, obj_cls=1)
                # read the data back and make sure its correct
                length = len(array_value_data[0])
                read_back_data = self.container.container.read_an_array(
                    len(array_value_data), length + 1, dkey, akey, obj)

                for data_index in range(3):
                    if array_value_data[data_index][0:length - 1] \
                            != read_back_data[data_index][0:length - 1]:
                        self.log.info("Written Data: %s", array_value_data[data_index])
                        self.log.info("Read Data:    %s", read_back_data[data_index])
                        self.fail("Data mismatch")
                # test case 1
                if index == 0:
                    try:
                        # write single value data to same keys, expected to fail
                        self.container.container.write_an_obj(
                            single_value_data, len(single_value_data) + 1,
                            dkey, akey, obj, obj_cls=1)
                        # above line is expected to return an error,
                        # if not fail the test
                        self.fail("Single value write to existing array value"
                                  + " key should have failed\n")

                    # should fail with -1001 ERR
                    except DaosApiError as error:
                        if "-1001" not in str(error):
                            self.log.error(error)
                            self.fail("Expected write to fail with -1001")

                # test case 2 and 3
                else:
                    try:
                        # punch the keys
                        obj.punch_akeys(0, dkey, [akey])
                        obj.punch_dkeys(0, [dkey])

                        if aggregation:
                            # trigger aggregation
                            self.container.container.aggregate(self.container.container.coh, 0)

                        # write to the same set of keys under same object
                        # with single value type
                        self.container.container.write_an_obj(
                            single_value_data, len(single_value_data) + 1,
                            dkey, akey, obj, obj_cls=1)
                    # above write of array value should either succeed
                    # or fail with -1001 ERR
                    except DaosApiError as error:
                        if "-1001" not in str(error):
                            self.log.error(error)
                            self.fail("Expected write to either succeed or fail with -1001")

                    # change the value of aggregation to test Test Case 3
                    aggregation = True

                # punch the entire object after each iteration
                obj.close()

            # catch the exception if test fails to write to an object
            # or fails to punch the written object
            except DaosApiError as error:
                self.log.error(error)
                self.fail("Failed to write to akey/dkey or punch the object")
