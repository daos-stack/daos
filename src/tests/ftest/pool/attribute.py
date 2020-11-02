#!/usr/bin/python
'''
  (C) Copyright 2020 Intel Corporation.

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

import traceback
import threading
import string
import random
from apricot import TestWithServers

from general_utils import DaosTestError
from pydaos.raw import DaosApiError
from test_utils_pool import TestPool

# pylint: disable=global-variable-not-assigned, global-statement

GLOB_SIGNAL = None
GLOB_RC = -99000000


def cb_func(event):
    """Callback Function for asynchronous mode."""
    global GLOB_SIGNAL
    global GLOB_RC
    GLOB_RC = event.event.ev_error
    GLOB_SIGNAL.set()


def verify_list_attr(indata, size, buff):
    """
    verify the length of the Attribute names
    """
    # length of all the attribute names
    aggregate_len = sum(len(attr_name) for attr_name in indata.keys()) + 1
    # there is a space between each name, so account for that
    aggregate_len += len(indata.keys())-1

    if aggregate_len != size:
        raise DaosTestError("FAIL: Size is not matching for Names in list"
                            "attr, Expected len={0} and received len = {1}"
                            .format(aggregate_len, size))
    # verify the Attributes names in list_attr retrieve
    for key in indata.keys():
        if key not in buff:
            raise DaosTestError("FAIL: Name does not match after list attr,"
                                " Expected buf={0} and received buf = {1}"
                                .format(key, buff))


def verify_get_attr(indata, outdata):
    """
    verify the Attributes value after get_attr
    """
    for attr, value in indata.iteritems():
        if value != outdata[attr]:
            raise DaosTestError("FAIL: Value does not match after get attr,"
                                " Expected val={0} and received val = {1}"
                                .format(value, outdata[attr]))


class PoolAttributeTest(TestWithServers):
    """
    Test class Description: Tests DAOS pool attribute get/set/list.
    :avocado: recursive
    """

    def setUp(self):
        super(PoolAttributeTest, self).setUp()

        self.large_data_set = {}

        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)
        self.pool.create()
        self.pool.connect()

    def create_data_set(self):
        """
        To create the large attribute dictionary
        """
        allchar = string.ascii_letters + string.digits
        for i in range(1024):
            self.large_data_set[str(i)] = (
                "".join(random.choice(allchar)
                        for x in range(random.randint(1, 100))))

    def test_pool_large_attributes(self):
        """
        Test ID: DAOS-1359

        Test description: Test large randomly created pool attribute.

        :avocado: tags=regression,pool,pool_attr,attribute,large_poolattribute
        """
        self.create_data_set()
        attr_dict = self.large_data_set

        try:
            self.pool.pool.set_attr(data=attr_dict)
            size, buf = self.pool.pool.list_attr()

            verify_list_attr(attr_dict, size.value, buf)

            results = {}
            results = self.pool.pool.get_attr(attr_dict.keys())
            verify_get_attr(attr_dict, results)
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test was expected to pass but it failed.\n")

    def test_pool_attributes(self):
        """
        Test ID: DAOS-1359

        Test description: Test basic pool attribute tests (sync).

        :avocado: tags=all,pool,pr,tiny,sync_poolattribute
        """
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

        attr_dict = {name[0]: value[0]}
        try:
            self.pool.pool.set_attr(data=attr_dict)
            size, buf = self.pool.pool.list_attr()

            verify_list_attr(attr_dict, size.value, buf)

            if name[0] is not None:
                # Request something that doesn't exist
                if "Negative" in name[0]:
                    name[0] = "rubbish"
                results = {}
                results = self.pool.pool.get_attr([name[0]])
                verify_get_attr(attr_dict, results)
            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")

    def test_pool_attribute_asyn(self):
        """
        Test ID: DAOS-1359

        Test description: Test basic pool attribute tests (async).

        :avocado: tags=all,pool,full_regression,tiny,async_poolattribute
        """
        global GLOB_SIGNAL
        global GLOB_RC

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
