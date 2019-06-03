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
import sys
import ctypes
import avocado
from apricot import Test, skipForTicket


from daos_api import DaosPool, DaosContainer, IORequest
from daos_api import DaosApiError

class CreateManyDkeys(Test):
    """
    Test Class Description:
        Tests that create large numbers of keys in objects/containers and then
        destroy the containers and verify the space has been reclaimed.

    :avocado: recursive
    """
    def setUp(self):
        super(CreateManyDkeys, self).setUp()
        self.pool = DaosPool(self.context)
        self.pool.create(self.params.get("mode", '/run/pool/createmode/*'),
                         os.geteuid(),
                         os.getegid(),
                         self.params.get("size", '/run/pool/createsize/*'),
                         self.params.get("setname", '/run/pool/createset/*'),
                         None)
        self.pool.connect(1 << 1)

    def tearDown(self):
        try:
            if self.pool:
                self.pool.destroy(1)
        finally:
            super(CreateManyDkeys, self).tearDown()

    def write_a_bunch_of_values(self, how_many):
        """
        Write data to an object, each with a dkey and akey.  The how_many
        parameter determines how many key:value pairs are written.
        """

        self.container = DaosContainer(self.context)
        self.container.create(self.pool.handle)
        self.container.open()

        ioreq = IORequest(self.context, self.container, None)
        epoch = self.container.get_new_epoch()
        c_epoch = ctypes.c_uint64(epoch)

        print("Started Writing the Dataset-----------\n")
        inc = 50000
        last_key = inc
        for key in range(how_many):
            c_dkey = ctypes.create_string_buffer("dkey {0}".format(key))
            c_akey = ctypes.create_string_buffer("akey {0}".format(key))
            c_value = ctypes.create_string_buffer(
                "some data that gets stored with the key {0}".format(key))
            c_size = ctypes.c_size_t(ctypes.sizeof(c_value))
            ioreq.single_insert(c_dkey,
                                c_akey,
                                c_value,
                                c_size,
                                c_epoch)

            if key > last_key:
                print("written: {}".format(key))
                sys.stdout.flush()
                last_key = key + inc

        self.container.commit_epoch(c_epoch)

        print("Started Verification of the Dataset-----------\n")
        last_key = inc
        for key in range(how_many):
            c_dkey = ctypes.create_string_buffer("dkey {0}".format(key))
            c_akey = ctypes.create_string_buffer("akey {0}".format(key))
            the_data = "some data that gets stored with the key {0}".format(key)
            val = ioreq.single_fetch(c_dkey,
                                     c_akey,
                                     len(the_data)+1,
                                     c_epoch)

            if the_data != (repr(val.value)[1:-1]):
                self.fail("ERROR: Data mismatch for dkey = {0}, akey={1}, "
                          "Expected Value={2} and Received Value={3}\n"
                          .format("dkey {0}".format(key),
                                  "akey {0}".format(key),
                                  the_data,
                                  repr(val.value)[1:-1]))

            if key > last_key:
                print("veried: {}".format(key))
                sys.stdout.flush()
                last_key = key + inc

        print("starting destroy")
        self.container.close()
        self.container.destroy()
        print("destroy complete")

    @avocado.fail_on(DaosApiError)
    @skipForTicket("DAOS-1721")
    def test_many_dkeys(self):
        """
        Test ID: DAOS-1701
        Test Description: Test many of dkeys in same object.
        Use Cases: 1. large key counts
                   2. space reclaimation after destroy
        :avocado: tags=object,vm,many_dkeys

        """

        no_of_dkeys = self.params.get("number_of_dkeys", '/run/dkeys/')

        # write a lot of individual data items, verify them, then destroy
        self.write_a_bunch_of_values(no_of_dkeys)


        # do it again, which should verify the first container
        # was truely destroyed because a second round won't fit otherwise
        self.write_a_bunch_of_values(no_of_dkeys)
