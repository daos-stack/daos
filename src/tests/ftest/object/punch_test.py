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

from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError


class PunchTest(TestWithServers):
    """
    Simple test to verify the 3 different punch calls.
    :avocado: recursive
    """
    def setUp(self):
        super(PunchTest, self).setUp()
        self.prepare_pool()

        try:
            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.pool.handle)

            # now open it
            self.container.open()
        except DaosApiError as excpn:
            print(excpn)
            print(traceback.format_exc())
            self.fail("Test failed during setup.\n")

    def test_dkey_punch(self):
        """
        The most basic test of the dkey punch function.

        :avocado: tags=all,object,pr,small,dkeypunch
        """

        try:
            # create an object and write some data into it
            thedata = "a string that I want to stuff into an object"
            dkey = "this is the dkey"
            akey = "this is the akey"
            tx_handle = self.container.get_new_tx()
            print("Created a new TX for punch dkey test")

            obj = self.container.write_an_obj(thedata, len(thedata)+1, dkey,
                                              akey, obj_cls=1, txn=tx_handle)
            print("Committing the TX for punch dkey test")
            self.container.commit_tx(tx_handle)
            print("Committed the TX for punch dkey test")

            # read the data back and make sure its correct
            thedata2 = self.container.read_an_obj(len(thedata)+1, dkey, akey,
                                                  obj, txn=tx_handle)
            if thedata != thedata2.value:
                print("data I wrote:" + thedata)
                print("data I read back" + thedata2.value)
                self.fail("Wrote data, read it back, didn't match\n")

            # now punch this data, should fail, can't punch committed data
            obj.punch_dkeys(tx_handle, [dkey])

            # expecting punch of commit data above to fail
            self.fail("Punch should have failed but it didn't.\n")

        # expecting an exception so do nothing
        except DaosApiError as dummy_e:
            pass

        try:
            self.container.close_tx(tx_handle)
            print("Closed TX for punch dkey test")

            # now punch this data
            obj.punch_dkeys(0, [dkey])

        # this one should work so error if exception occurs
        except DaosApiError as dummy_e:
            self.fail("Punch should have worked.\n")

        # there are a bunch of other cases to test here,
        #    --test punching the same updating and punching the same data in
        #    the same tx, should fail
        #    --test non updated data in an open tx, should work

    def test_akey_punch(self):
        """
        The most basic test of the akey punch function.

        :avocado: tags=all,object,pr,small,akeypunch
        """

        try:
            # create an object and write some data into it
            dkey = "this is the dkey"
            data1 = [("this is akey 1", "this is data value 1"),
                     ("this is akey 2", "this is data value 2"),
                     ("this is akey 3", "this is data value 3")]
            tx_handle = self.container.get_new_tx()
            print("Created a new TX for punch akey test")
            obj = self.container.write_multi_akeys(dkey, data1, obj_cls=1,
                                                   txn=tx_handle)
            print("Committing the TX for punch akey test")
            self.container.commit_tx(tx_handle)
            print("Committed the TX for punch dkey test")

            # read back the 1st epoch's data and check 1 value just to make sure
            # everything is on the up and up
            readbuf = [(data1[0][0], len(data1[0][1]) + 1),
                       (data1[1][0], len(data1[1][1]) + 1),
                       (data1[2][0], len(data1[2][1]) + 1)]
            retrieved_data = self.container.read_multi_akeys(dkey, readbuf, obj,
                                                             txn=tx_handle)
            if retrieved_data[data1[1][0]] != data1[1][1]:
                print("middle akey: {}".format(retrieved_data[data1[1][0]]))
                self.fail("data retrieval failure")

            # now punch one akey from this data
            obj.punch_akeys(tx_handle, dkey, [data1[1][0]])

            # expecting punch of commit data above to fail
            self.fail("Punch should have failed but it didn't.\n")

        # expecting an exception so do nothing
        except DaosApiError as excep:
            print(excep)

        try:
            self.container.close_tx(tx_handle)
            print("Closed TX for punch akey test")

            # now punch the object without a tx
            obj.punch_akeys(0, dkey, [data1[1][0]])

        # expecting it to work this time so error
        except DaosApiError as excep:
            self.fail("Punch should have worked: {}\n".format(excep))

    def test_obj_punch(self):
        """
        The most basic test of the object punch function.  Really similar
        to above except the whole object is deleted.

        :avocado: tags=all,object,pr,small,objpunch
        """

        try:

            # create an object and write some data into it
            thedata = "a string that I want to stuff into an object"
            dkey = "this is the dkey"
            akey = "this is the akey"
            tx_handle = self.container.get_new_tx()
            print("Created a new TX for punch obj test")
            obj = self.container.write_an_obj(thedata, len(thedata)+1, dkey,
                                              akey, obj_cls=1, txn=tx_handle)
            print("Committing the TX for punch obj test")
            self.container.commit_tx(tx_handle)
            print("Committed the TX for punch obj test")
            # read the data back and make sure its correct
            thedata2 = self.container.read_an_obj(len(thedata)+1, dkey, akey,
                                                  obj, txn=tx_handle)
            if thedata != thedata2.value:
                print("data I wrote:" + thedata)
                print("data I read back" + thedata2.value)
                self.fail("Wrote data, read it back, didn't match\n")

            # now punch the object, committed so not expecting it to work
            obj.punch(tx_handle)

            # expecting punch of commit data above to fail
            self.fail("Punch should have failed but it didn't.\n")

        # expecting an exception so do nothing
        except DaosApiError as excep:
            print(excep)

        try:
            self.container.close_tx(tx_handle)
            print("Closed TX for punch obj test")

            obj.punch(0)

        # expecting it to work without a tx
        except DaosApiError as excep:
            print(excep)
            self.fail("Punch should have worked.\n")
