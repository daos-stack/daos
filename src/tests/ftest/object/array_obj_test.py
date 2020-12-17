#!/usr/bin/python
'''
  (C) Copyright 2017-2020 Intel Corporation.

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

import time
import traceback
import logging

from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError, c_uuid_to_str


class ArrayObjTest(TestWithServers):
    """
    Test Class Description:
    A very simple test verifying the ability to read/write arrays to an object.
    :avocado: recursive
    """
    def setUp(self):
        super(ArrayObjTest, self).setUp()
        self.plog = logging.getLogger("progress")

    def test_array_obj(self):
        """
        Test ID: DAOS-961

        Test Description: Writes an array to an object and then reads it
        back and verifies it.

        :avocado: tags=all,smoke,daily_regression,object,tiny,basicobject
        """
        self.prepare_pool()

        try:
            # create a container
            container = DaosContainer(self.context)
            container.create(self.pool.pool.handle)
            self.plog.info("Container %s created.", container.get_uuid_str())

            # now open it
            container.open()

            # do a query and compare the UUID returned from create with
            # that returned by query
            container.query()

            if container.get_uuid_str() != c_uuid_to_str(
                    container.info.ci_uuid):
                self.fail("Container UUID did not match the one in info\n")

            # create an object and write some data into it
            thedata = []
            thedata.append("data string one")
            thedata.append("data string two")
            thedata.append("data string tre")
            dkey = "this is the dkey"
            akey = "this is the akey"

            self.plog.info("writing array to dkey >%s< akey >%s<.", dkey, akey)
            oid = container.write_an_array_value(thedata, dkey, akey,
                                                 obj_cls=3)

            # read the data back and make sure its correct
            length = len(thedata[0])
            thedata2 = container.read_an_array(len(thedata), length+1,
                                               dkey, akey, oid)
            if thedata[0][0:length-1] != thedata2[0][0:length-1]:
                self.plog.error("Data mismatch")
                self.plog.error("Wrote: >%s<", thedata[0])
                self.plog.error("Read: >%s<", thedata2[0])
                self.fail("Write data, read it back, didn't match\n")

            if thedata[2][0:length-1] != thedata2[2][0:length-1]:
                self.plog.error("Data mismatch")
                self.plog.error("Wrote: >%s<", thedata[2])
                self.plog.error("Read: >%s<", thedata2[2])
                self.fail("Write data, read it back, didn't match\n")

            container.close()

            # wait a few seconds and then destroy
            time.sleep(5)
            container.destroy()

            self.plog.info("Test Complete")

        except DaosApiError as excep:
            self.plog.error("Test Failed, exception was thrown.")
            print(excep)
            print(traceback.format_exc())
            self.fail("Test was expected to pass but it failed.\n")
