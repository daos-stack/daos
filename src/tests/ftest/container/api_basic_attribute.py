#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback

from apricot import TestWithServers
from general_utils import DaosTestError
from pydaos.raw import DaosApiError
from test_utils_base import CallbackHandler


class ContainerAPIBasicAttributeTest(TestWithServers):
    """
    Tests DAOS container attribute get/set/list.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ContainerAttributeTest object."""
        super().__init__(*args, **kwargs)
        self.attr_name = None
        self.attr_val = None
        self.attr_dict = {}
        self.expected_result = None

    def verify_list_attr(self, in_dict, out_size, out_buf, is_async=True):
        """Verify list_attr output; size and buf.

        Args:
            in_dict (dict): Dictionary that stores the Name-Value pairs that's used
                in set_attr().
            out_size (int): Size returned by list_attr().
            out_buf (str): Buffer returned by list_attr(). It stores the names in the
                following format.
                b'Name1\x00Name2\x00Name3\x00\x00'
                Each name has following \x00 and one \x00 at the end.
            is_async (bool): Whether list_attr() was called with asynchronous mode.
                Defaults to True.
        """
        self.log.info("in_dict = %s", in_dict)
        self.log.info("out_size = %d", out_size)
        self.log.info("out_buf = %s", out_buf)

        # Create the list of names from the input dictionary and the list_attr() output
        # and sort them.
        in_names = sorted(list(in_dict.keys()))
        out_names = sorted(out_buf.split(b"\x00")[:-2])

        self.log.info("in_names = %s", in_names)
        self.log.info("out_names = %s", out_names)

        # In order to verify the size, calculate the total number of characters of all
        # the names in the input dictionary.
        total_str_len = sum(len(name) for name in in_names)
        # Add the number of \x00 because the size includes them.
        total_zero_buf_len = len(in_names)
        # Size from the async mode is one larger than sync. It correctly counts the
        # number of "\x00"s.
        if is_async:
            total_zero_buf_len += 1
        expected_size = total_str_len + total_zero_buf_len

        self.log.info("total_str_len = %d", total_str_len)
        self.log.info("total_zero_buf_len = %d", total_zero_buf_len)
        self.log.info("expected_size = %d", expected_size)

        msg = "Unexpected size from list_attr! Expected = {}; Actual = {}".format(
            expected_size, out_size)
        self.assertEqual(expected_size, out_size, msg)

        msg = ("Unexpected output from list_attr! Expected = {}; Actual raw = {};"
               "Actual in list format = {}".format(in_names, out_buf, out_names))
        self.assertEqual(out_names, in_names, msg)

    def verify_get_attr(self, indata, outdata):
        """Verify the Attributes value after get_attr.

        Args:
            indata (dict): Name-value pairs that were used for set-attr.
            outdata (dict): Name-value pairs that were obtained from get-attr.
        """
        decoded = {}
        for key, val in outdata.items():
            decoded[key.decode()] = val

        self.log.info("Verifying get_attr output:")
        self.log.info("  get_attr data: %s", indata)
        self.log.info("  set_attr data: %s", decoded)

        for attr, value in indata.items():
            if value != decoded.get(attr.decode(), None):
                self.fail(
                    "FAIL: Value does not match after get({}), Expected "
                    "val={} and received val={}".format(
                        attr, value, decoded.get(attr.decode(), None)))

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

    def prepare_attribute_test(self):
        """Prepare variables needed for the tests.
        """
        self.add_pool()
        self.add_container(self.pool)
        self.container.open()

        expected_for_param = []
        name = self.params.get("name", '/run/attrtests/name_handles/*/')
        self.attr_name = name[0]
        expected_for_param.append(name[1])
        value = self.params.get("value", '/run/attrtests/value_handles/*/')
        self.attr_val = value[0]
        expected_for_param.append(value[1])

        # Convert any test yaml string to bytes
        if isinstance(self.attr_name, str):
            self.attr_name = self.attr_name.encode("utf-8")
        if isinstance(self.attr_val, str):
            self.attr_val = self.attr_val.encode("utf-8")

        self.attr_dict = {self.attr_name: self.attr_val}

        self.expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                self.expected_result = 'FAIL'
                break

    def test_basic_attribute_sync(self):
        """Test ID: DAOS-1359

        Test container set/list/get attribute synchronously.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container,container_attribute
        :avocado: tags=container_api_basic_attribute_sync,test_basic_attribute_sync
        """
        self.prepare_attribute_test()

        # Test set_attr()
        try:
            self.container.container.set_attr(data=self.attr_dict)

            # One exception is when we use the "Negative-Name" and valid value. In this
            # case, the method works, but we consider it as failure during get_attr test,
            # so temporarily use "PASS" here.
            if self.attr_name and b"Negative" in self.attr_name and self.attr_val:
                set_exp_result = "PASS"
            else:
                set_exp_result = self.expected_result
            if set_exp_result == 'FAIL':
                self.fail("set_attr() was expected to fail but it worked!")
        except DaosApiError as error:
            print(error)
            print(traceback.format_exc())
            if self.expected_result == "PASS":
                self.fail(
                    "set_attr was supposed to work, but failed! name-value = {}".format(
                        self.attr_dict))

        # Test list_attr()
        try:
            size, buf = self.container.container.list_attr()
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("list_attr failed!")

        if self.expected_result == 'PASS':
            self.verify_list_attr(
                in_dict=self.attr_dict, out_size=size, out_buf=buf, is_async=False)

        # Test get_attr()
        # Request something that doesn't exist
        if self.attr_name and b"Negative" in self.attr_name:
            self.attr_name = b"rubbish"

        try:
            results = self.container.container.get_attr(attr_names=[self.attr_name])

            if self.expected_result == 'FAIL':
                self.fail("get_attr() was expected to fail but it worked!")
        except (DaosApiError, DaosTestError) as excep:
            print(excep)
            print(traceback.format_exc())
            if self.expected_result == 'PASS':
                msg = "get_attr was supposed to work, but failed! attr_name = {}".format(
                    self.attr_name)
                self.fail(msg)

        if self.expected_result == "PASS":
            # Verify the get_attr output.
            self.verify_get_attr(indata=self.attr_dict, outdata=results)

    def test_basic_attribute_async(self):
        """Test ID: DAOS-1359

        Test container set/list/get attribute asynchronously.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container,container_attribute
        :avocado: tags=container_api_basic_attribute_async,test_basic_attribute_async
        """
        self.prepare_attribute_test()

        callback_handler = CallbackHandler()

        # Test set_attr()
        try:
            self.container.container.set_attr(
                data=self.attr_dict, coh=None, cb_func=callback_handler.callback)
            callback_handler.wait()

            # Verify the return code.
            # One exception is when we use the "Negative-Name" and valid value. In this
            # case, the method works, but we consider it as failure during get_attr test,
            # so temporarily use "PASS" here.
            if self.attr_name and b"Negative" in self.attr_name and self.attr_val:
                set_exp_result = "PASS"
            else:
                set_exp_result = self.expected_result
            self.verify_async_return_code(
                ret_code=callback_handler.ret_code, expected_result=set_exp_result,
                method_name="set_attr")
        except DaosApiError as error:
            print(error)
            print(traceback.format_exc())
            if self.expected_result == "PASS":
                self.fail(
                    "set_attr was supposed to work, but failed! name-value = {}".format(
                        self.attr_dict))

        # Test list_attr()
        try:
            size, buf = self.container.container.list_attr(
                cb_func=callback_handler.callback)
            callback_handler.wait()

            # Verify the return code.
            ret_code = callback_handler.ret_code
            if ret_code != 0:
                self.fail("Unexpected RC after list_attr! {}".format(ret_code))
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("list_attr failed!")

        if self.expected_result == 'PASS':
            self.verify_list_attr(in_dict=self.attr_dict, out_size=size, out_buf=buf)

        # Test get_attr()
        # Request something that doesn't exist
        if self.attr_name and b"Negative" in self.attr_name:
            self.attr_name = b"rubbish"

        try:
            buff, sizes = self.container.container.get_attr(
                attr_names=[self.attr_name], coh=None, cb_func=callback_handler.callback)
            callback_handler.wait()

            # Construct the result dictionary from buff and sizes.
            results = {self.attr_name: buff[0][:sizes[0]]}

            # Verify the return code.
            self.verify_async_return_code(
                ret_code=callback_handler.ret_code, expected_result=self.expected_result,
                method_name="get_attr")
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if self.expected_result == 'PASS':
                msg = "get_attr was supposed to work, but failed! attr_name = {}".format(
                    self.attr_name)
                self.fail(msg)

        if self.expected_result == "PASS":
            # Verify the get_attr output.
            self.verify_get_attr(indata=self.attr_dict, outdata=results)
