#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback
import threading
import random

from apricot import TestWithServers
from general_utils import DaosTestError, get_random_bytes
from pydaos.raw import DaosApiError

# pylint: disable = global-variable-not-assigned, global-statement
GLOB_SIGNAL = None
GLOB_RC = -99000000


def cb_func(event):
    """Call back Function for asynchronous mode."""
    global GLOB_SIGNAL
    global GLOB_RC
    GLOB_RC = event.event.ev_error
    GLOB_SIGNAL.set()


class ContainerAttributeTest(TestWithServers):
    """
    Tests DAOS container attribute get/set/list.
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

    def verify_list_attr(self, indata, size, buff, mode="sync"):
        """
        verify the length of the Attribute names
        """
        length = sum(
            len(i) if i is not None else 0 for i in list(indata.keys()))
        length += len(list(indata.keys()))
        if mode == "async":
            length = length + 1

        self.log.info("Verifying list_attr output:")
        self.log.info("  set_attr names:  %s", list(indata.keys()))
        self.log.info("  set_attr size:   %s", length)
        self.log.info("  list_attr names: %s", buff)
        self.log.info("  list_attr size:  %s", size)

        if length != size:
            self.fail(
                "FAIL: Size is not matching for Names in list attr, Expected "
                "len={} and received len={}".format(length, size))
        # verify the Attributes names in list_attr retrieve
        for key in list(indata.keys()):
            if key not in buff:
                self.fail(
                    "FAIL: Name does not match after list attr, Expected "
                    "buf={} and received buf={}".format(key, buff))

    def verify_get_attr(self, indata, outdata):
        """
        verify the Attributes value after get_attr
        """
        self.log.info("Verifying get_attr output:")
        self.log.info("  get_attr data: %s", indata)
        self.log.info("  set_attr date: %s", outdata)

        for attr, value in indata.items():
            if value != outdata[attr]:
                self.fail(
                    "FAIL: Value does not match after get attr, Expected "
                    "val={} and received val={}".format(value, outdata[attr]))

    def test_container_large_attributes(self):
        """
        Test ID: DAOS-1359

        Test description: Test large randomly created container attribute.

        :avocado: tags=container,container_attr,attribute,large_conattribute
        """
        self.add_pool()
        self.add_container(self.pool)
        self.container.open()

        attr_dict = self.create_data_set()

        try:
            self.container.container.set_attr(data=attr_dict)
            size, buf = self.container.container.list_attr()

            self.verify_list_attr(attr_dict, size, buf)

            results = self.container.container.get_attr(list(attr_dict.keys()))
            self.verify_get_attr(attr_dict, results)
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test was expected to pass but it failed.\n")

    def test_container_attribute(self):
        """
        Test basic container attribute tests.
        :avocado: tags=all,tiny,full_regression,container,sync_conattribute
        """
        self.add_pool()
        self.add_container(self.pool)
        self.container.open()

        expected_for_param = []
        name = self.params.get("name", '/run/attrtests/name_handles/*/')
        expected_for_param.append(name[1])
        value = self.params.get("value", '/run/attrtests/value_handles/*/')
        expected_for_param.append(value[1])

        # Convert any test yaml string to bytes
        if isinstance(name[0], str):
            name[0] = name[0].encode("utf-8")
        if isinstance(value[0], str):
            value[0] = value[0].encode("utf-8")

        attr_dict = {name[0]: value[0]}

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            self.container.container.set_attr(data=attr_dict)
            size, buf = self.container.container.list_attr()

            self.verify_list_attr(attr_dict, size, buf)

            # Request something that doesn't exist
            if name[0] is not None and b"Negative" in name[0]:
                name[0] = b"rubbish"

            results = {}
            results = self.container.container.get_attr([name[0]])

            self.verify_get_attr(attr_dict, results)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except (DaosApiError, DaosTestError) as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")

    def test_container_attribute_asyn(self):
        """
        Test basic container attribute tests.

        :avocado: tags=all,small,full_regression,container,async_conattribute
        """
        global GLOB_SIGNAL
        global GLOB_RC

        self.add_pool()
        self.add_container(self.pool)
        self.container.open()

        expected_for_param = []
        name = self.params.get("name", '/run/attrtests/name_handles/*/')
        expected_for_param.append(name[1])
        value = self.params.get("value", '/run/attrtests/value_handles/*/')
        expected_for_param.append(value[1])

        # Convert any test yaml string to bytes
        if isinstance(name[0], str):
            name[0] = name[0].encode("utf-8")
        if isinstance(value[0], str):
            value[0] = value[0].encode("utf-8")

        attr_dict = {name[0]: value[0]}

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            GLOB_SIGNAL = threading.Event()
            self.container.container.set_attr(data=attr_dict, cb_func=cb_func)
            GLOB_SIGNAL.wait()
            if GLOB_RC != 0 and expected_result in ['PASS']:
                self.fail("RC not as expected after set_attr First {0}"
                          .format(GLOB_RC))

            GLOB_SIGNAL = threading.Event()
            size, buf = self.container.container.list_attr(cb_func=cb_func)
            GLOB_SIGNAL.wait()
            if GLOB_RC != 0 and expected_result in ['PASS']:
                self.fail("RC not as expected after list_attr First {0}"
                          .format(GLOB_RC))
            if expected_result in ['PASS']:
                self.verify_list_attr(attr_dict, size, buf, mode="async")

            # Request something that doesn't exist
            if name[0] is not None and b"Negative" in name[0]:
                name[0] = b"rubbish"

            GLOB_SIGNAL = threading.Event()
            self.container.container.get_attr([name[0]], cb_func=cb_func)

            GLOB_SIGNAL.wait()

            if GLOB_RC != 0 and expected_result in ['PASS']:
                self.fail("RC not as expected after get_attr {0}"
                          .format(GLOB_RC))

            # not verifying the get_attr since its not available asynchronously

            if value[0] is not None:
                if GLOB_RC == 0 and expected_result in ['FAIL']:
                    self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
