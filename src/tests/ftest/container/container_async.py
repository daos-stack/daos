#!/usr/bin/python
'''
  (C) Copyright 2018-2020 Intel Corporation.

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
import uuid
import threading

from apricot import TestWithServers, skipForTicket
from pydaos.raw import  DaosContainer, DaosApiError


# pylint: disable=global-variable-not-assigned, global-statement
GLOB_SIGNAL = None
GLOB_RC = -99000000


def cb_func(event):
    """
    Callback function for asynchronous container functionality.
    """
    global GLOB_SIGNAL
    global GLOB_RC

    GLOB_RC = event.event.ev_error
    GLOB_SIGNAL.set()


class ContainerAsync(TestWithServers):
    """
    Tests DAOS pool connect permissions (non existing pool handle, bad uuid)
    and close.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        super(ContainerAsync, self).__init__(*args, **kwargs)
        self.container1 = None
        self.container2 = None

    @skipForTicket("DAOS-4106")
    def test_createasync(self):
        """
        Test container create for asynchronous mode.

        :avocado: tags=all,small,full_regression,container,createasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        # initialize a python pool object then create the underlying
        # daos storage
        self.prepare_pool()
        poh = self.pool.pool.handle

        try:
            # Container initialization and creation
            self.container1 = DaosContainer(self.context)
            self.container2 = DaosContainer(self.context)

            GLOB_SIGNAL = threading.Event()
            self.container1.create(poh=poh, con_uuid=None, cb_func=cb_func)

            GLOB_SIGNAL.wait()
            if GLOB_RC != 0:
                self.fail("RC not as expected in async test")
            print("RC after successful container create: ", GLOB_RC)

            # Try to recreate container after destroying pool,
            # this should fail. Checking rc after failure.
            self.pool.destroy(1)
            GLOB_SIGNAL = threading.Event()
            GLOB_RC = -9900000
            self.container2.create(poh, None, cb_func)

            GLOB_SIGNAL.wait()
            if GLOB_RC == 0:
                self.fail("RC not as expected in async test")
            print("RC after unsuccessful container create: ", GLOB_RC)

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_destroyasync(self):
        """
        Test container destroy for asynchronous mode.

        :avocado: tags=all,small,full_regression,container,contdestroyasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        # initialize a python pool object then create the underlying
        # daos storage
        self.prepare_pool()
        poh = self.pool.pool.handle

        try:
            # Container initialization and creation
            self.container1 = DaosContainer(self.context)
            self.container2 = DaosContainer(self.context)

            self.container1.create(poh)

            GLOB_SIGNAL = threading.Event()
            self.container1.destroy(1, poh, None, cb_func)

            GLOB_SIGNAL.wait()
            if GLOB_RC != 0:
                self.fail("RC not as expected in async test")
            print("RC after successful container create: ", GLOB_RC)

            # Try to destroy container again, this should fail, as non-existent.
            # Checking rc after failure.
            GLOB_SIGNAL = threading.Event()
            GLOB_RC = -9900000
            self.container2.destroy(1, poh, None, cb_func)

            GLOB_SIGNAL.wait()
            if GLOB_RC != -1003:
                self.fail("RC not as expected in async test")
            print("RC after container destroy failed:", GLOB_RC)
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_openasync(self):
        """
        Test container open for asynchronous mode.

        :avocado: tags=all,small,full_regression,container,openasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        # initialize a python pool object then create the underlying
        # daos storage
        self.prepare_pool()
        poh = self.pool.pool.handle

        try:
            # Container initialization and creation
            self.container1 = DaosContainer(self.context)
            self.container2 = DaosContainer(self.context)

            self.container1.create(poh)

            str_cuuid = self.container1.get_uuid_str()
            cuuid = uuid.UUID(str_cuuid)

            GLOB_SIGNAL = threading.Event()
            self.container1.open(poh, cuuid, 2, cb_func)

            GLOB_SIGNAL.wait()
            if GLOB_RC != 0:
                self.fail("RC not as expected in async test")
            print("RC after successful container create: ", GLOB_RC)

            # Try to open container2, this should fail, as non-existent.
            # Checking rc after failure.
            GLOB_SIGNAL = threading.Event()
            GLOB_RC = -9900000
            self.container2.open(None, None, None, cb_func)

            GLOB_SIGNAL.wait()
            if GLOB_RC == 0:
                self.fail("RC not as expected in async test")
            print("RC after container destroy failed:", GLOB_RC)

            # cleanup the container
            self.container1.close()
            self.container1.destroy()
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_closeasync(self):
        """
        Test container close for asynchronous mode.

        :avocado: tags=all,small,full_regression,container,closeasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        # initialize a python pool object then create the underlying
        # daos storage
        self.prepare_pool()
        poh = self.pool.pool.handle

        try:
            # Container initialization and creation
            self.container1 = DaosContainer(self.context)
            self.container2 = DaosContainer(self.context)

            self.container1.create(poh)

            str_cuuid = self.container1.get_uuid_str()
            cuuid = uuid.UUID(str_cuuid)

            self.container1.open(poh, cuuid, 2)

            GLOB_SIGNAL = threading.Event()
            self.container1.close(cb_func=cb_func)

            GLOB_SIGNAL.wait()
            if GLOB_RC != 0:
                self.fail("RC not as expected in async test: "
                          "{0}".format(GLOB_RC))
            print("RC after successful container create: ", GLOB_RC)

            # Try to open container2, this should fail, as non-existent.
            # Checking rc after failure.
            GLOB_SIGNAL = threading.Event()
            GLOB_RC = -9900000
            self.container2.close(cb_func=cb_func)

            GLOB_SIGNAL.wait()
            if GLOB_RC == 0:
                self.fail("RC not as expected in async test: "
                          "{0}".format(GLOB_RC))
            print("RC after container destroy failed:", GLOB_RC)

            # cleanup the container
            self.container1.destroy()
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_queryasync(self):
        """
        Test container query for asynchronous mode.

        :avocado: tags=all,small,full_regression,container,queryasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        # initialize a python pool object then create the underlying
        # daos storage
        self.prepare_pool()
        poh = self.pool.pool.handle

        try:
            # Container initialization and creation
            self.container1 = DaosContainer(self.context)
            self.container2 = DaosContainer(self.context)

            self.container1.create(poh)

            dummy_str_cuuid = self.container1.get_uuid_str()

            # Open container
            self.container1.open(poh, None, 2, None)

            GLOB_SIGNAL = threading.Event()
            self.container1.query(cb_func=cb_func)

            GLOB_SIGNAL.wait()
            if GLOB_RC != 0:
                self.fail("RC not as expected in async test: "
                          "{0}".format(GLOB_RC))
            print("RC after successful container create: ", GLOB_RC)

            # Close opened container
            self.container1.close()

            # Try to open container2, this should fail, as non-existent.
            # Checking rc after failure.
            GLOB_SIGNAL = threading.Event()
            GLOB_RC = -9900000
            self.container2.query(cb_func=cb_func)

            GLOB_SIGNAL.wait()
            if GLOB_RC == 0:
                self.fail("RC not as expected in async test: "
                          "{0}".format(GLOB_RC))
            print("RC after container destroy failed:", GLOB_RC)

            # cleanup the container
            self.container1.destroy()
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
