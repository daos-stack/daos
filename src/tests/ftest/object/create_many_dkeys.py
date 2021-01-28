#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import print_function

import sys
import ctypes
import avocado

from apricot import TestWithServers
from pydaos.raw import DaosContainer, IORequest, DaosApiError


class CreateManyDkeys(TestWithServers):
    """
    Test Class Description:
        Tests that create large numbers of keys in objects/containers and then
        destroy the containers and verify the space has been reclaimed.

    :avocado: recursive
    """

    def write_a_bunch_of_values(self, how_many):
        """
        Write data to an object, each with a dkey and akey.  The how_many
        parameter determines how many key:value pairs are written.
        """

        self.container = DaosContainer(self.context)
        self.container.create(self.pool.pool.handle)
        self.container.open()

        ioreq = IORequest(self.context, self.container, None)

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
                                c_size)

            if key > last_key:
                print("written: {}".format(key))
                sys.stdout.flush()
                last_key = key + inc

        print("Started Verification of the Dataset-----------\n")
        last_key = inc
        for key in range(how_many):
            c_dkey = ctypes.create_string_buffer("dkey {0}".format(key))
            c_akey = ctypes.create_string_buffer("akey {0}".format(key))
            the_data = "some data that gets stored with the key {0}".format(key)
            val = ioreq.single_fetch(c_dkey,
                                     c_akey,
                                     len(the_data)+1)

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
    def test_many_dkeys(self):
        """
        Test ID: DAOS-1701
        Test Description: Test many of dkeys in same object.
        Use Cases: 1. large key counts
                   2. space reclamation after destroy
        :avocado: tags=all,full,small,object,many_dkeys

        """
        self.prepare_pool()
        no_of_dkeys = self.params.get("number_of_dkeys", '/run/dkeys/')

        # write a lot of individual data items, verify them, then destroy
        self.write_a_bunch_of_values(no_of_dkeys)


        # do it again, which should verify the first container
        # was truly destroyed because a second round won't fit otherwise
        self.write_a_bunch_of_values(no_of_dkeys)
