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
import traceback
import threading
import string
import random
from apricot import TestWithServers

from general_utils import DaosTestError

from daos_api import DaosPool, DaosContainer, DaosApiError

GLOB_SIGNAL = None
GLOB_RC = -99000000

def cb_func(event):
    "Callback Function for asynchronous mode"
    global GLOB_SIGNAL
    global GLOB_RC
    GLOB_RC = event.event.ev_error
    GLOB_SIGNAL.set()

def verify_list_attr(indata, size, buff, mode="sync"):
    """
    verify the length of the Attribute names
    """
    length = sum(len(i) for i in indata.keys()) + len(indata.keys())
    if mode == "async":
        length = length + 1
    if length != size:
        raise DaosTestError("FAIL: Size is not matching for Names in list"
                            "attr, Expected len={0} and received len = {1}"
                            .format(length, size))
    #verify the Attributes names in list_attr retrieve
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

class ContainerAttributeTest(TestWithServers):
    """
    Tests DAOS container attribute get/set/list.
    :avocado: recursive
    """
    def setUp(self):
        super(ContainerAttributeTest, self).setUp()

        self.large_data_set = {}

        self.pool = DaosPool(self.context)
        self.pool.create(self.params.get("mode", '/run/attrtests/createmode/*'),
                         os.geteuid(),
                         os.getegid(),
                         self.params.get("size", '/run/attrtests/createsize/*'),
                         self.params.get("setname",
                                         '/run/attrtests/createset/*'),
                         None)
        self.pool.connect(1 << 1)
        poh = self.pool.handle
        self.container = DaosContainer(self.context)
        self.container.create(poh)
        self.container.open()

    def tearDown(self):
        try:
            if self.container:
                self.container.close()
        finally:
            super(ContainerAttributeTest, self).tearDown()

    def create_data_set(self):
        """
        To create the large attribute dictionary
        """
        allchar = string.ascii_letters + string.digits
        for i in range(1024):
            self.large_data_set[str(i)] = (
                "".join(random.choice(allchar)
                        for x in range(random.randint(1, 100))))

    def test_container_attribute(self):
        """
        Test basic container attribute tests.
        :avocado: tags=container,container_attr,attribute,sync_conattribute
        """
        expected_for_param = []
        name = self.params.get("name", '/run/attrtests/name_handles/*/')
        expected_for_param.append(name[1])
        value = self.params.get("value", '/run/attrtests/value_handles/*/')
        expected_for_param.append(value[1])

        attr_dict = {name[0]:value[0]}
        if name[0] is not None:
            if "largenumberofattr" in name[0]:
                self.create_data_set()
                attr_dict = self.large_data_set
                attr_dict[name[0]] = value[0]

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            self.container.set_attr(data=attr_dict)
            size, buf = self.container.list_attr()

            verify_list_attr(attr_dict, size, buf)

            # Request something that doesn't exist
            if name[0] is not None and "Negative" in name[0]:
                name[0] = "rubbish"

            results = {}
            results = self.container.get_attr([name[0]])

            # for this test the dictionary has been altered, need to just
            # set it to what we are expecting to get back
            if name[0] is not None:
                if "largenumberofattr" in name[0]:
                    attr_dict.clear()
                    attr_dict[name[0]] = value[0]

            verify_get_attr(attr_dict, results)

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

        :avocado: tags=container,container_attr,attribute,async_conattribute
        """
        global GLOB_SIGNAL
        global GLOB_RC

        expected_for_param = []
        name = self.params.get("name", '/run/attrtests/name_handles/*/')
        expected_for_param.append(name[1])
        value = self.params.get("value", '/run/attrtests/value_handles/*/')
        expected_for_param.append(value[1])

        attr_dict = {name[0]:value[0]}
        if name[0] is not None:
            if "largenumberofattr" in name[0]:
                self.create_data_set()
                attr_dict = self.large_data_set
                attr_dict[name[0]] = value[0]

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            GLOB_SIGNAL = threading.Event()
            self.container.set_attr(data=attr_dict, cb_func=cb_func)
            GLOB_SIGNAL.wait()
            if GLOB_RC != 0 and expected_result in ['PASS']:
                self.fail("RC not as expected after set_attr First {0}"
                          .format(GLOB_RC))

            GLOB_SIGNAL = threading.Event()
            size, buf = self.container.list_attr(cb_func=cb_func)
            GLOB_SIGNAL.wait()
            if GLOB_RC != 0 and expected_result in ['PASS']:
                self.fail("RC not as expected after list_attr First {0}"
                          .format(GLOB_RC))
            if expected_result in ['PASS']:
                verify_list_attr(attr_dict, size, buf, mode="async")

            # Request something that doesn't exist
            if name[0] is not None and "Negative" in name[0]:
                name[0] = "rubbish"

            GLOB_SIGNAL = threading.Event()
            self.container.get_attr([name[0]], cb_func=cb_func)

            GLOB_SIGNAL.wait()

            if GLOB_RC != 0 and expected_result in ['PASS']:
                self.fail("RC not as expected after get_attr {0}"
                          .format(GLOB_RC))

            # not verifying the get_attr since its not available asynchronously

            if value[0] != None:
                if GLOB_RC == 0 and expected_result in ['FAIL']:
                    self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
