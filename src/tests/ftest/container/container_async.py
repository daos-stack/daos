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
import time
import traceback
import uuid
import threading

from apricot import TestWithServers

from daos_api import  DaosPool, DaosContainer, DaosApiError


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

    def tearDown(self):
        try:
            if self.pool is not None and self.pool.attached:
                self.pool.destroy(1)
        finally:
            time.sleep(5)
            super(ContainerAsync, self).tearDown()

    def test_createasync(self):
        """
        Test container create for asynchronous mode.

        :avocado: tags=container,containerasync,createasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/createtests/createmode/*/')
        createsetid = self.params.get("setname", '/run/createtests/createset/')
        createsize = self.params.get("size", '/run/createtests/createsize/')
        createuid = os.geteuid()
        creategid = os.getegid()

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)

            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)

            poh = self.pool.handle

            self.pool.connect(1 << 1)

            # Container initialization and creation
            self.container1 = DaosContainer(self.context)
            self.container2 = DaosContainer(self.context)


            GLOB_SIGNAL = threading.Event()
            self.container1.create(poh, None, cb_func)

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

            # cleanup the pool and container
            self.pool = None

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_destroyasync(self):
        """
        Test container destroy for asynchronous mode.

        :avocado: tags=container,containerasync,contdestroyasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/createtests/createmode/*/')
        createsetid = self.params.get("setname", '/run/createtests/createset/')
        createsize = self.params.get("size", '/run/createtests/createsize/')
        createuid = os.geteuid()
        creategid = os.getegid()

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)

            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)

            poh = self.pool.handle

            self.pool.connect(1 << 1)

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

            # cleanup the pool and container
            self.pool.disconnect()
            self.pool.destroy(1)
            self.pool = None

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_openasync(self):
        """
        Test container open for asynchronous mode.

        :avocado: tags=container,containerasync,openasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/createtests/createmode/*/')
        createsetid = self.params.get("setname", '/run/createtests/createset/')
        createsize = self.params.get("size", '/run/createtests/createsize/')
        createuid = os.geteuid()
        creategid = os.getegid()

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)

            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)

            poh = self.pool.handle

            self.pool.connect(1 << 1)

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

            # cleanup the pool and container
            self.container1.close()
            self.container1.destroy()
            self.pool.disconnect()
            self.pool.destroy(1)
            self.pool = None

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_closeasync(self):
        """
        Test container close for asynchronous mode.

        :avocado: tags=container,containerasync,closeasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/createtests/createmode/*/')
        createsetid = self.params.get("setname", '/run/createtests/createset/')
        createsize = self.params.get("size", '/run/createtests/createsize/')
        createuid = os.geteuid()
        creategid = os.getegid()

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)

            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)


            poh = self.pool.handle

            self.pool.connect(1 << 1)

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

            # cleanup the pool and container
            self.container1.destroy()
            self.pool.disconnect()
            self.pool.destroy(1)
            self.pool = None

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())

    def test_queryasync(self):
        """
        Test container query for asynchronous mode.

        :avocado: tags=container,containerasync,queryasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/createtests/createmode/*/')
        createsetid = self.params.get("setname", '/run/createtests/createset/')
        createsize = self.params.get("size", '/run/createtests/createsize/')
        createuid = os.geteuid()
        creategid = os.getegid()

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)

            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)

            poh = self.pool.handle

            self.pool.connect(1 << 1)

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

            # cleanup the pool and container
            self.container1.destroy()
            self.pool.disconnect()
            self.pool.destroy(1)
            self.pool = None

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
