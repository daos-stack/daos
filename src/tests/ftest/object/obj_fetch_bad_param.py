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
from avocado import main
from apricot import TestWithServers


from daos_api import DaosPool, DaosContainer, DaosApiError

class ObjFetchBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_fetch function.
    :avocado: recursive
    """
    def setUp(self):
        super(ObjFetchBadParam, self).setUp()
        time.sleep(5)

        try:
            # parameters used in pool create
            createmode = self.params.get("mode", '/run/pool/createmode/')
            createsetid = self.params.get("setname", '/run/pool/createset/')
            createsize = self.params.get("size", '/run/pool/createsize/')
            createuid = os.geteuid()
            creategid = os.getegid()

            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid, createsize,
                             createsetid, None)

            # need a connection to create container
            self.pool.connect(1 << 1)

            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.handle)

            # now open it
            self.container.open()

            # create an object and write some data into it
            thedata = "a string that I want to stuff into an object"
            self.datasize = len(thedata) + 1
            self.dkey = "this is the dkey"
            self.akey = "this is the akey"
            self.obj, self.epoch = self.container.write_an_obj(thedata,
                                                               self.datasize,
                                                               self.dkey,
                                                               self.akey, None,
                                                               None, 2)

            thedata2 = self.container.read_an_obj(self.datasize, self.dkey,
                                                  self.akey, self.obj,
                                                  self.epoch)
            if thedata not in thedata2.value:
                print(thedata)
                print(thedata2.value)
                self.fail("Error reading back data, test failed during"\
                         " the initial setup.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test failed during the initial setup.\n")

    def tearDown(self):
        try:
            if self.container:
                self.container.close()
                self.container.destroy()
            if self.pool:
                self.pool.disconnect()
                self.pool.destroy(1)
        finally:
            super(ObjFetchBadParam, self).tearDown()

    def test_bad_handle(self):
        """
        Test ID: DAOS-1377

        Test Description: Pass a bogus object handle, should return bad handle.

        :avocado: tags=object,objfetch,objfetchbadhand,regression,vm,small
        """

        try:
            # trash the handle and read again
            saved_oh = self.obj.obj_handle
            self.obj.obj_handle = 99999

            # expecting this to fail with -1002
            dummy_thedata2 = self.container.read_an_obj(self.datasize,
                                                        self.dkey, self.akey,
                                                        self.obj, self.epoch)

            self.container.oh = saved_oh
            self.fail("Test was expected to return a -1002 but it has not.\n")

        except DaosApiError as excep:
            self.container.oh = saved_oh
            if '-1002' not in str(excep):
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -1002 but it has not.\n")

    def test_null_ptrs(self):
        """
        Test ID: DAOS-1377

        Test Description: Pass null pointers for various fetch parameters.

        :avocado: tags=object,objfetch,objfetchnull,regression,vm,small
        """
        try:
            # now try it with a bad dkey, expecting this to fail with -1003
            dummy_thedata2 = self.container.read_an_obj(self.datasize, None,
                                                        self.akey, self.obj,
                                                        self.epoch)

            self.container.close()
            self.container.destroy()
            self.pool.disconnect()
            self.pool.destroy(1)
            self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")

        try:
            # now try it with a null sgl (iod_size is not set)
            # expecting this to fail with -2013
            test_hints = ['sglnull']
            dummy_thedata2 = self.container.read_an_obj(self.datasize,
                                                        self.dkey, self.akey,
                                                        self.obj, self.epoch,
                                                        test_hints)

            # behavior not as expect so commented out for now
            # when DAOS-1448 is complete, uncomment and retest

            self.fail("Test was expected to return a -2013 but it has not.\n")

        except DaosApiError as excep:
            if '-2013' not in str(excep):
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -2013 but it has not.\n")

        try:
            # when DAOS-1449 is complete, uncomment and retest
            # now try it with a null iod, expecting this to fail with -1003
            #test_hints = ['iodnull']
            #thedata2 = self.container.read_an_obj(self.datasize, dkey, akey,
            #                                 self.obj, self.epoch, test_hints)
            pass
            #self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")


if __name__ == "__main__":
    main()
