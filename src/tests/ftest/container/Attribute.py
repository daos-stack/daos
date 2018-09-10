#!/usr/bin/python
'''
  (C) Copyright 2018 Intel Corporation.

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

import os
import traceback
import sys
import json
import threading
import string
import random
from avocado       import Test

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import ServerUtils
import WriteHostFile

from daos_api import DaosContext, DaosPool, DaosContainer

GLOB_SIGNAL = None
GLOB_RC = -99000000

def cb_func(event):
    "Callback Function for asynchronous mode"
    global GLOB_SIGNAL
    global GLOB_RC
    GLOB_RC = event.event.ev_error
    GLOB_SIGNAL.set()

def verify_list_attr(indata, size, buffer, mode):
    """
    verify the length of the Attribute names
    """
    length = sum(len(i) for i in indata.keys()) + len(indata.keys())
    if mode == "async":
        length = length + 1
    if length != size:
        raise ValueError("FAIL: Size is not matching for Names in list"
                         "attr, Expected len={0} and received len = {1}"
                         .format(length, size))
    #verify the Attributes names in list_attr retrieve
    for key in indata.keys():
        if key not in buffer:
            raise ValueError("FAIL: Name does not match after list attr,"
                             " Expected buf={0} and received buf = {1}"
                             .format(key, buffer))
    print ("===== list Attr name = {0} and length = {1}"
           .format(buffer, size))

def verify_get_attr(indata, outdata):
    """
    verify the Attributes value after get_attr
    """
    final_val = []
    for j in range(0, len(indata)):
        final_val.append(outdata[j])

    print ("===== get Attr value ")
    for i in range(0, len(indata)):
        if str(indata.values()[i]) not in final_val:
            raise ValueError("FAIL: Value does not match after get attr,"
                             " Expected val={0} and received val = {1}"
                             .format(indata.keys()[i], indata.values()[i]))
        else:
            print indata.keys()[i], indata.values()[i]

class ContainerAttributeTest(Test):
    """
    Tests DAOS container attribute get/set/list.
    """
    def setUp(self):
        with open('../../../.build_vars.json') as f:
            build_paths = json.load(f)
        basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        tmp = build_paths['PREFIX'] + '/tmp'
        server_group = self.params.get("server_group",
                                       '/server/',
                                       'daos_server')
        self.Context = DaosContext(build_paths['PREFIX'] + '/lib/')

        self.hostlist = self.params.get("test_machines", '/run/hosts/*')
        self.hostfile = WriteHostFile.WriteHostFile(self.hostlist, tmp)

        ServerUtils.runServer(self.hostfile, server_group, basepath)

        self.POOL = DaosPool(self.Context)
        self.POOL.create(self.params.get("mode", '/run/attrtests/createmode/*'),
                         os.geteuid(),
                         os.getegid(),
                         self.params.get("size", '/run/attrtests/createsize/*'),
                         self.params.get("setname", '/run/attrtests/createset/*'),
                         None)
        self.POOL.connect(1 << 1)
        poh = self.POOL.handle
        self.CONTAINER = DaosContainer(self.Context)
        self.CONTAINER.create(poh)
        self.CONTAINER.open()
        self.large_data_set = {}

    def tearDown(self):
        if self.hostfile is not None:
            os.remove(self.hostfile)
        self.CONTAINER.close()
        ServerUtils.stopServer()

    def create_data_set(self):
        """
        To create the large attribute dictionary
        """
        allchar = string.ascii_letters + string.digits
        for i in range(1024):
            self.large_data_set[str(i)] = "".join(random.choice(allchar)
                                                  for x in range(random.randint(1, 100)))

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

        attr_dict = dict(zip(name, value))
        if name[0] is not None:
            if "largenumberofattr" in name[0]:
                self.create_data_set()
                attr_dict = self.large_data_set

        if 'PASS' in attr_dict:
            del attr_dict['PASS']
        if 'FAIL' in attr_dict:
            del attr_dict['FAIL']

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            print ("===== Set Attr")
            self.CONTAINER.set_attr(data=attr_dict)
            size, buf = self.CONTAINER.list_attr()

            verify_list_attr(attr_dict, size, buf)

            ##This is for requesting the name which is not exist.
            if "Negative" in name[0]:
                attr_dict["Wrong_Value"] = attr_dict.pop(name[0])

            val = self.CONTAINER.get_attr(data=attr_dict)
            verify_get_attr(attr_dict, val)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except Exception as e:
            print (e)
            print (traceback.format_exc())
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

        attr_dict = dict(zip(name, value))
        if name[0] is not None:
            if "largenumberofattr" in name[0]:
                self.create_data_set()
                attr_dict = self.large_data_set

        if 'PASS' in attr_dict:
            del attr_dict['PASS']
        if 'FAIL' in attr_dict:
            del attr_dict['FAIL']

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            print ("===== Set Attr")
            GLOB_SIGNAL = threading.Event()
            self.CONTAINER.set_attr(data=attr_dict, cb_func=cb_func)
            GLOB_SIGNAL.wait()
            if GLOB_RC != 0:
                self.fail("RC not as expected after set_attr First {0}"
                          .format(GLOB_RC))

            GLOB_SIGNAL = threading.Event()
            size, buf = self.CONTAINER.list_attr(cb_func=cb_func)
            GLOB_SIGNAL.wait()
            if GLOB_RC != 0:
                self.fail("RC not as expected after list_attr First {0}"
                          .format(GLOB_RC))
            verify_list_attr(attr_dict, size, buf, mode="async")

            #This is for requesting the name which is not exist.
            if "Negative" in name[0]:
                attr_dict["Wrong_Value"] = attr_dict.pop(name[0])

            GLOB_SIGNAL = threading.Event()
            val = self.CONTAINER.get_attr(data=attr_dict, cb_func=cb_func)
            GLOB_SIGNAL.wait()
            if GLOB_RC != 0:
                self.fail("RC not as expected after get_attr {0}"
                          .format(GLOB_RC))
            verify_get_attr(attr_dict, val)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except Exception as e:
            print (e)
            print (traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
