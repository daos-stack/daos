"""
  (C) Copyright 2019-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import ctypes

from apricot import TestWithServers
from general_utils import create_string_buffer
from pydaos.raw import DaosObj, IORequest
from test_utils_container import add_container
from test_utils_pool import add_pool


class CsumContainerValidation(TestWithServers):
    """
    Test Class Description: This test is enables
    checksum container properties and performs
    single object inserts and verifies
    contents. This is a basic sanity
    test for enabling checksum testing.
    :avocado: recursive
    """

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
        no_of_dkeys = self.params.get("no_of_dkeys", '/run/dkeys/*')[0]
        no_of_akeys = self.params.get("no_of_akeys", '/run/akeys/*')[0]
        record_length = self.params.get("length", '/run/record/*')

        pool = add_pool(self, connect=False)
        pool.connect(2)

        enable_checksum = self.params.get("enable_checksum", '/run/container/*')
        container = add_container(self, pool, create=False)
        container.input_params.enable_chksum = enable_checksum
        container.create()
        container.open()

        obj = DaosObj(self.context, container.container)
        obj.create(objcls=1)
        obj.open()
        ioreq = IORequest(self.context, container.container, obj, objtype=4)

        self.d_log.info("Writing the Single Dataset")
        record_index = 0
        for dkey in range(no_of_dkeys):
            for akey in range(no_of_akeys):
                indata = "{0}".format(str(akey)[0]) * record_length[record_index]
                c_dkey = create_string_buffer("dkey {0}".format(dkey))
                c_akey = create_string_buffer("akey {0}".format(akey))
                c_value = create_string_buffer(indata)
                c_size = ctypes.c_size_t(ctypes.sizeof(c_value))

                ioreq.single_insert(c_dkey, c_akey, c_value, c_size)
                record_index = record_index + 1
                if record_index == len(record_length):
                    record_index = 0

        self.d_log.info("Single Dataset Verification -- Started")
        record_index = 0
        for dkey in range(no_of_dkeys):
            for akey in range(no_of_akeys):
                indata = str(akey)[0] * record_length[record_index]
                c_dkey = create_string_buffer("dkey {0}".format(dkey))
                c_akey = create_string_buffer("akey {0}".format(akey))
                val = ioreq.single_fetch(c_dkey, c_akey, len(indata) + 1)
                if indata != val.value.decode('utf-8'):
                    message = (
                        "ERROR:Data mismatch for dkey={}, akey={}: indata={}, "
                        "val={}".format(
                            dkey, akey, indata, val.value.decode('utf-8')))
                    self.d_log.error(message)
                    self.fail(message)
                record_index = record_index + 1
                if record_index == len(record_length):
                    record_index = 0
