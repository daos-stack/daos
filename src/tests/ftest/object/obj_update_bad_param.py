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
import traceback
import logging
from avocado import main
from apricot import TestWithServers


from daos_api import DaosPool, DaosContainer, DaosApiError

class ObjUpdateBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_update function.
    :avocado: recursive
    """
    def setUp(self):
        super(ObjUpdateBadParam, self).setUp()
        self.plog = logging.getLogger("progress")

    def test_bad_handle(self):
        """
        Test ID: DAOS-1376

        Test Description: Pass a bogus object handle, should return bad handle.

        :avocado: tags=object,objupdate,objbadhand,regression,vm,small
        """
        try:
            # parameters used in pool create
            createmode = self.params.get("mode", '/run/conttests/createmode/')
            createsetid = self.params.get("setname",
                                          '/run/conttests/createset/')
            createsize = self.params.get("size", '/run/conttests/createsize/')
            createuid = os.geteuid()
            creategid = os.getegid()

            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(self.context)
            pool.create(createmode, createuid, creategid,
                        createsize, createsetid, None)
            self.plog.info("Pool %s created.", pool.get_uuid_str())

            # need a connection to create container
            pool.connect(1 << 1)

            # create a container
            container = DaosContainer(self.context)
            container.create(pool.handle)
            self.plog.info("Container %s created.", container.get_uuid_str())

            # now open it
            container.open()

            # create an object and write some data into it
            thedata = "a string that I want to stuff into an object"
            thedatasize = len(thedata) + 1
            dkey = "this is the dkey"
            akey = "this is the akey"
            obj, dummy_tx = container.write_an_obj(thedata, thedatasize,
                                                   dkey, akey, None, None, 2)

            saved_oh = obj.obj_handle
            obj.obj_handle = 99999

            obj, dummy_tx = container.write_an_obj(thedata, thedatasize,
                                                   dkey, akey, obj, None, 2)

            container.oh = saved_oh
            container.close()
            container.destroy()
            pool.disconnect()
            pool.destroy(1)
            self.fail("Test was expected to return a -1002 but it has not.\n")

        except DaosApiError as excep:
            container.oh = saved_oh
            container.close()
            container.destroy()
            pool.disconnect()
            pool.destroy(1)
            self.plog.info("Test Complete")
            if '-1002' not in str(excep):
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -1002 but it has not.\n")

    def test_null_values(self):
        """
        Test ID: DAOS-1376

        Test Description: Pass a dkey and an akey that is null.

        :avocado: tags=object,objupdate,objupdatenull,regression,vm,small
        """
        try:
            # parameters used in pool create
            createmode = self.params.get("mode", '/run/conttests/createmode/')
            createsetid = self.params.get("setname",
                                          '/run/conttests/createset/')
            createsize = self.params.get("size", '/run/conttests/createsize/')
            createuid = os.geteuid()
            creategid = os.getegid()

            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(self.context)
            pool.create(createmode, createuid, creategid,
                        createsize, createsetid, None)
            self.plog.info("Pool %s created.", pool.get_uuid_str())

            # need a connection to create container
            pool.connect(1 << 1)

            # create a container
            container = DaosContainer(self.context)
            container.create(pool.handle)
            self.plog.info("Container %s created.", container.get_uuid_str())

            # now open it
            container.open()

            # data used in the test
            thedata = "a string that I want to stuff into an object"
            thedatasize = len(thedata) + 1

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test failed during setup .\n")

        try:
            # try using a null dkey
            dkey = None
            akey = "this is the akey"

            container.write_an_obj(thedata, thedatasize, dkey, akey, None,
                                   None, 2)

            container.close()
            container.destroy()
            pool.disconnect()
            pool.destroy(1)
            self.plog.error("Didn't get expected return code.")
            self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                container.close()
                container.destroy()
                pool.disconnect()
                pool.destroy(1)
                self.plog.error("Didn't get expected return code.")
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")

        try:
            # try using a null akey/io descriptor
            dkey = "this is the dkey"
            akey = None
            container.write_an_obj(thedata, thedatasize, dkey, akey, None,
                                   None, 2)
            self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                self.plog.error("Didn't get expected return code.")
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")

        try:
            # lastly try passing no data
            thedata = None
            thedatasize = 0
            dkey = "this is the dkey"
            akey = "this is the akey"

            container.write_an_obj(thedata, thedatasize, dkey, akey, None,
                                   None, 2)
            self.plog.info("Update with no data worked")

        except DaosApiError as excep:
            container.close()
            container.destroy()
            pool.disconnect()
            pool.destroy(1)
            print(excep)
            print(traceback.format_exc())
            self.plog.error("Update with no data failed")
            self.fail("Update with no data failed.\n")

        container.close()
        container.destroy()
        pool.disconnect()
        pool.destroy(1)
        self.plog.info("Test Complete")

if __name__ == "__main__":
    main()
