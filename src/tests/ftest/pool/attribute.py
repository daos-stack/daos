#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback
import threading
import random
from apricot import TestWithServers

from general_utils import get_random_bytes
from pydaos.raw import DaosApiError

# pylint: disable=global-variable-not-assigned, global-statement
GLOB_SIGNAL = None
GLOB_RC = -99000000


def cb_func(event):
    """Callback Function for asynchronous mode."""
    global GLOB_SIGNAL
    global GLOB_RC
    GLOB_RC = event.event.ev_error
    GLOB_SIGNAL.set()


class PoolAttributeTest(TestWithServers):
    """Pool Attribute test Class.

    Test class Description:
        Tests DAOS pool attribute get/set/list.

    :avocado: recursive
    """

    @staticmethod
    def create_data_set():
        """Create the large attribute dictionary.

        Returns:
            dict: a large attribute dictionary

        """
        data_set = {}
        for index in range(1024):
            size = random.randint(1, 100)
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

    def test_pool_large_attributes(self):
        """Test pool attributes with large data set.

        Test ID: DAOS-1359

        Test description: Test large randomly created pool attribute.

        :avocado: tags=regression,pool,pool_attr,attribute,large_poolattribute
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

        :avocado: tags=all,pool,daily_regression,tiny,sync_poolattribute
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

    def test_pool_attribute_asyn(self):
        """Test pool attribute with callback.

        Test ID: DAOS-1359

        Test description: Test basic pool attribute tests (async).

        :avocado: tags=all,pool,full_regression,tiny,async_poolattribute
        """
        global GLOB_SIGNAL
        global GLOB_RC

        self.add_pool()
        expected_for_param = []
        name = self.params.get("name", '/run/attrtests/name_handles/*/')
        # workaround until async functions are fixed
        if name[0] is not None and "Negative" in name[0]:
            pass
        else:
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
            GLOB_SIGNAL = threading.Event()
            self.pool.pool.set_attr(attr_dict, None, cb_func)
            GLOB_SIGNAL.wait()
            if expected_result == 'PASS' and GLOB_RC != 0:
                self.fail("RC not as expected after set_attr {0}"
                          .format(GLOB_RC))
            if expected_result == 'FAIL' and GLOB_RC == 0:
                self.fail("RC not as expected after set_attr {0}"
                          .format(GLOB_RC))

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
