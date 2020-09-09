#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

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
"""

import ctypes
from pydaos.raw import (DaosContainer, IORequest,
                        DaosObj)
from apricot import TestWithServers


class ChecksumContainerValidation(TestWithServers):
    """
    Test Class Description: This test is enables
    checksum container properties and performs
    single object inserts and verifies
    contents. This is a basic sanity
    test for enabling checksum testing.
    :avocado: recursive
    """
    # pylint: disable=too-many-instance-attributes
    def setUp(self):
        super(ChecksumContainerValidation, self).setUp()
        self.agent_sessions = None
        self.pool = None
        self.container = None
        self.obj = None
        self.ioreq = None
        self.no_of_dkeys = None
        self.no_of_akeys = None
        self.array_size = None
        self.record_length = None

        self.no_of_dkeys = self.params.get("no_of_dkeys", '/run/dkeys/*')[0]
        self.no_of_akeys = self.params.get("no_of_akeys", '/run/akeys/*')[0]
        self.record_length = self.params.get("length", '/run/record/*')

        self.add_pool(connect=False)
        self.pool.connect(2)

        self.csum = self.params.get("enable_checksum", '/run/container/*')
        self.container = DaosContainer(self.context)
        input_param = self.container.cont_input_values
        input_param.enable_chksum = self.csum
        self.container.create(poh=self.pool.pool.handle,
                              con_prop=input_param)
        self.container.open()

        self.obj = DaosObj(self.context, self.container)
        self.obj.create(objcls=1)
        self.obj.open()
        self.ioreq = IORequest(self.context,
                               self.container,
                               self.obj, objtype=4)

    def test_single_object_with_checksum(self):
        """
        Test ID: DAOS-3927
        Test Description: Write Avocado Test to verify single data after
                          pool/container disconnect/reconnect.
        :avocado: tags=all,full_regression,pr,basic_checksum_object
        """
        self.d_log.info("Writing the Single Dataset")
        record_index = 0
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = ("{0}".format(str(akey)[0])
                          * self.record_length[record_index])
                c_dkey = ctypes.create_string_buffer("dkey {0}".format(dkey))
                c_akey = ctypes.create_string_buffer("akey {0}".format(akey))
                c_value = ctypes.create_string_buffer(indata)
                c_size = ctypes.c_size_t(ctypes.sizeof(c_value))

                self.ioreq.single_insert(c_dkey, c_akey, c_value, c_size)
                record_index = record_index + 1
                if record_index == len(self.record_length):
                    record_index = 0

        self.d_log.info("Single Dataset Verification -- Started")
        record_index = 0
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = ("{0}".format(str(akey)[0]) *
                          self.record_length[record_index])
                c_dkey = ctypes.create_string_buffer("dkey {0}".format(dkey))
                c_akey = ctypes.create_string_buffer("akey {0}".format(akey))
                val = self.ioreq.single_fetch(c_dkey,
                                              c_akey,
                                              len(indata)+1)
                if indata != (repr(val.value)[1:-1]):
                    self.d_log.error("ERROR:Data mismatch for "
                                     "dkey = {0}, "
                                     "akey = {1}".format(
                                         "dkey {0}".format(dkey),
                                         "akey {0}".format(akey)))
                    self.fail("ERROR: Data mismatch for dkey = {0}, akey={1}"
                              .format("dkey {0}".format(dkey),
                                      "akey {0}".format(akey)))

                record_index = record_index + 1
                if record_index == len(self.record_length):
                    record_index = 0
