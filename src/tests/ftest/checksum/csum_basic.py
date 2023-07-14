"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import ctypes
from pydaos.raw import (DaosContainer, IORequest,
                        DaosObj)
from apricot import TestWithServers
from general_utils import create_string_buffer


class CsumContainerValidation(TestWithServers):
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
        super().setUp()
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
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=checksum
        :avocado: tags=CsumContainerValidation,test_single_object_with_checksum
        """
        self.d_log.info("Writing the Single Dataset")
        record_index = 0
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = ("{0}".format(str(akey)[0])
                          * self.record_length[record_index])
                c_dkey = create_string_buffer("dkey {0}".format(dkey))
                c_akey = create_string_buffer("akey {0}".format(akey))
                c_value = create_string_buffer(indata)
                c_size = ctypes.c_size_t(ctypes.sizeof(c_value))

                self.ioreq.single_insert(c_dkey, c_akey, c_value, c_size)
                record_index = record_index + 1
                if record_index == len(self.record_length):
                    record_index = 0

        self.d_log.info("Single Dataset Verification -- Started")
        record_index = 0
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = str(akey)[0] * self.record_length[record_index]
                c_dkey = create_string_buffer("dkey {0}".format(dkey))
                c_akey = create_string_buffer("akey {0}".format(akey))
                val = self.ioreq.single_fetch(c_dkey, c_akey, len(indata) + 1)
                if indata != val.value.decode('utf-8'):
                    message = (
                        "ERROR:Data mismatch for dkey={}, akey={}: indata={}, "
                        "val={}".format(
                            dkey, akey, indata, val.value.decode('utf-8')))
                    self.d_log.error(message)
                    self.fail(message)
                record_index = record_index + 1
                if record_index == len(self.record_length):
                    record_index = 0
