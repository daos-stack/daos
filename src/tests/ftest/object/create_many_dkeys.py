'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import ctypes
import sys

import avocado
from apricot import TestWithServers
from general_utils import create_string_buffer
from pydaos.raw import DaosApiError, IORequest
from test_utils_container import add_container
from test_utils_pool import add_pool


class CreateManyDkeys(TestWithServers):
    """
    Test Class Description:
        Tests that create large numbers of keys in objects/containers and then
        destroy the containers and verify the space has been reclaimed.

    :avocado: recursive
    """

    def write_a_bunch_of_values(self, pool, how_many):
        """Write data to an object, each with a dkey and akey.

        Args:
            pool (TestPool): the pool in which to write data
            how_many (int): how many key:value pairs are written
        """
        self.log_step("Creating a container")
        container = add_container(self, pool)
        container.open()

        ioreq = IORequest(self.context, container.container, None)

        self.log_step("Writing the dataset")
        inc = 50000
        last_key = inc
        for key in range(how_many):
            c_dkey = create_string_buffer("dkey {0}".format(key))
            c_akey = create_string_buffer("akey {0}".format(key))
            c_value = create_string_buffer(
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

        self.log_step("Verifying the dataset")
        last_key = inc
        for key in range(how_many):
            c_dkey = create_string_buffer("dkey {0}".format(key))
            c_akey = create_string_buffer("akey {0}".format(key))
            the_data = "some data that gets stored with the key {0}".format(key)
            val = ioreq.single_fetch(c_dkey, c_akey, len(the_data) + 1)
            exp_value = val.value.decode("utf-8")
            if the_data != exp_value:
                self.log.debug("Expected Value: %s", the_data)
                self.log.debug("Received Value: %s", exp_value)
                self.fail("ERROR: Data mismatch for dkey='dkey {key}', akey='akey {key}'")

            if key > last_key:
                print("verified: {}".format(key))
                sys.stdout.flush()
                last_key = key + inc

        self.log_step("Destroying the container")
        container.destroy()

    @avocado.fail_on(DaosApiError)
    def test_many_dkeys(self):
        """
        Test ID: DAOS-1701
        Test Description: Test many of dkeys in same object.
        Use Cases: 1. large key counts
                   2. space reclamation after destroy

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=CreateManyDkeys,test_many_dkeys
        """
        no_of_dkeys = self.params.get("number_of_dkeys", '/run/dkeys/')

        self.log_step("Creating a pool")
        pool = add_pool(self)

        # write a lot of individual data items, verify them, then destroy
        self.write_a_bunch_of_values(pool, no_of_dkeys)

        # do it again, which should verify the first container
        # was truly destroyed because a second round won't fit otherwise
        self.write_a_bunch_of_values(pool, no_of_dkeys)

        self.log.info('Test passed')
