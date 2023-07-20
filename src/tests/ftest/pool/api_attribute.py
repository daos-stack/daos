'''
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback

from pydaos.raw import DaosApiError

from apricot import TestWithServers
from general_utils import get_random_bytes
from test_utils_base import CallbackHandler


class PoolAPIAttributeTest(TestWithServers):
    """Pool Attribute test Class.

    Test class Description:
        Tests DAOS pool attribute get/set/list.

    :avocado: recursive
    """

    def create_data_set(self):
        """Create the large attribute dictionary.

        Returns:
            dict: a large attribute dictionary

        """
        data_set = {}
        for index in range(1024):
            size = self.random.randint(1, 100)
            key = str(index).encode("utf-8")
            data_set[key] = get_random_bytes(size)
        return data_set

    def verify_list_attr(self, indata, size, buff):
        """Verify the length of the Attribute names.

        Args:
            indata (dict): dictionary of data sent to set_attr
            size (int): size of attribute names from list_attr
            buff (bytearray): attribute names from list_attr
        """
        # length of all the attribute names
        aggregate_len = sum(
            len(attr_name) if attr_name is not None else 0
            for attr_name in list(indata.keys())) + 1
        # there is a space between each name, so account for that
        aggregate_len += len(list(indata.keys())) - 1

        self.log.info("Verifying list_attr output:")
        self.log.info("  set_attr names:  %s", list(indata.keys()))
        self.log.info("  set_attr size:   %s", aggregate_len)
        self.log.info("  list_attr names: %s", buff)
        self.log.info("  list_attr size:  %s", size)

        if aggregate_len != size:
            self.fail(
                "FAIL: Size is not matching for Names in list attr, Expected "
                "len={0} and received len={1}".format(aggregate_len, size))
        # verify the Attributes names in list_attr retrieve
        for key in list(indata.keys()):
            if key not in buff:
                self.fail(
                    "FAIL: Name does not match after list attr, Expected "
                    "buf={} and received buf={}".format(key, buff))

    def verify_get_attr(self, indata, outdata):
        """Verify the Attributes value after get_attr.

        Args:
            indata (dict): dictionary of data sent to set_attr
            outdata (dict): dictionary of data from get_attr
        """
        self.log.info("Verifying get_attr output:")
        self.log.info("  set_attr data: %s", indata)
        self.log.info("  get_attr date: %s", outdata)
        for attr, value in indata.items():
            if value != outdata[attr]:
                self.fail(
                    "FAIL: Value does not match after get attr, Expected "
                    "val={} and received val={}".format(value, outdata[attr]))

    def verify_async_return_code(self, ret_code, expected_result, method_name):
        """Verify return code from the asynchronous method call.

        Args:
            ret_code (int): Return code.
            expected_result (str): Expected result; either PASS or FAIL.
            method_name (str): Method called.
        """
        if expected_result == "PASS" and ret_code != 0:
            self.fail("Unexpected RC! Method: {}; RC: {}".format(method_name, ret_code))
        if expected_result == "FAIL" and ret_code == 0:
            self.fail("Successful RC(0) is returned after failure! Method: {}".format(
                method_name))

    def test_pool_large_attributes(self):
        """Test pool attributes with large data set.

        Test ID: DAOS-1359

        Test description: Test large randomly created pool attribute.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_attribute
        :avocado: tags=PoolAPIAttributeTest,test_pool_large_attributes
        """
        self.add_pool()
        attr_dict = self.create_data_set()

        try:
            self.pool.pool.set_attr(data=attr_dict)
            size, buf = self.pool.pool.list_attr()

            self.verify_list_attr(attr_dict, size.value, buf)

            results = {}
            results = self.pool.pool.get_attr(list(attr_dict.keys()))
            self.verify_get_attr(attr_dict, results)
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test was expected to pass but it failed.\n")

    def test_pool_attributes(self):
        """Test pool attributes.

        Test ID: DAOS-1359

        Test description: Test basic pool attribute tests (sync).

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_attribute
        :avocado: tags=PoolAPIAttributeTest,test_pool_attributes
        """
        self.add_pool()
        expected_for_param = []
        name = self.params.get("name", '/run/attrtests/name_handles/*/')
        expected_for_param.append(name[1])
        value = self.params.get("value", '/run/attrtests/value_handles/*/')
        expected_for_param.append(value[1])

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        # Convert any test yaml string to bytes
        if isinstance(name[0], str):
            name[0] = name[0].encode("utf-8")
        if isinstance(value[0], str):
            value[0] = value[0].encode("utf-8")

        attr_dict = {name[0]: value[0]}
        try:
            self.pool.pool.set_attr(data=attr_dict)
            size, buf = self.pool.pool.list_attr()

            self.verify_list_attr(attr_dict, size.value, buf)

            if name[0] is not None:
                # Request something that doesn't exist
                if b"Negative" in name[0]:
                    name[0] = b"rubbish"
                results = {}
                results = self.pool.pool.get_attr([name[0]])
                self.verify_get_attr(attr_dict, results)
            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")

    def test_pool_attribute_async(self):
        """Test pool set/get/list attribute with callback.

        Test ID: DAOS-1359

        Test Steps:
        1. Create a pool
        2. Call set_attr asynchronously.
        3. Call list_attr asynchronously and verify the returned list.
        4. Call get_attr asynchronously and verify the returned name and value.

        Return code from set_attr and get_attr. list_attr should always return 0.

        set_attr: (Valid is for long, special, and "Negative-Name".)
        Name     Value     RC
        Valid    Valid     0
        Valid    None      -1003
        None     Valid     -1003
        None     None      -1003

        get_attr:
        Name     Value     RC
        Valid    Valid     0
        Valid    None      -1005
        Wrong    Valid     -1005
        Wrong    None      -1005
        None     Valid     -1003
        None     None      -1003

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_attribute
        :avocado: tags=PoolAPIAttributeTest,test_pool_attribute_async
        """
        self.add_pool()

        expected_for_param = []

        name = self.params.get("name", '/run/attrtests/name_handles/*/')
        attr_name = name[0]
        expected_for_param.append(name[1])
        value = self.params.get("value", '/run/attrtests/value_handles/*/')
        attr_val = value[0]
        expected_for_param.append(value[1])

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        # Convert any test yaml string to bytes
        if isinstance(attr_name, str):
            attr_name = attr_name.encode("utf-8")
        if isinstance(attr_val, str):
            attr_val = attr_val.encode("utf-8")

        attr_dict = {attr_name: attr_val}

        callback_handler = CallbackHandler()

        # Test set_attr()
        try:
            self.pool.pool.set_attr(
                data=attr_dict, cb_func=callback_handler.callback)
            callback_handler.wait()

            # Verify the return code.
            # One exception is when we use the "Negative-Name" and valid value. In this
            # case, the method works, but we consider it as failure during get_attr test,
            # so temporarily use "PASS" here.
            if attr_name and b"Negative" in attr_name and attr_val:
                set_exp_result = "PASS"
            else:
                set_exp_result = expected_result
            self.verify_async_return_code(
                ret_code=callback_handler.ret_code, expected_result=set_exp_result,
                method_name="set_attr")
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail(
                    "set_attr was supposed to work, but failed! name-value = {}".format(
                        attr_dict))

        # Test list_attr()
        try:
            size, buf = self.pool.pool.list_attr(cb_func=callback_handler.callback)
            callback_handler.wait()

            # Verify the return code.
            ret_code = callback_handler.ret_code
            if ret_code != 0:
                self.fail("Unexpected RC after list_attr! {}".format(ret_code))
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == "PASS":
                self.fail("list_attr failed!")

        if expected_result == "PASS":
            # Verify the list output.
            self.verify_list_attr(attr_dict, size.value, buf)

        # Verify get_attr()
        results = {}
        if attr_name and b"Negative" in attr_name:
            attr_name = b"rubbish"

        try:
            (buff, sizes) = self.pool.pool.get_attr(
                attr_names=[attr_name], cb_func=callback_handler.callback)
            callback_handler.wait()

            # Construct the result dictionary from buff and sizes.
            results = {attr_name: buff[0][:sizes[0]]}

            # Verify the return code.
            self.verify_async_return_code(
                ret_code=callback_handler.ret_code, expected_result=expected_result,
                method_name="get_attr")
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == "PASS":
                self.fail("get_attr failed! name = {}".format(attr_name))

        if expected_result == "PASS":
            # Verify the get_attr output.
            self.verify_get_attr(attr_dict, results)
