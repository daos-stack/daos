'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from apricot import TestWithServers
from pydaos.raw import DaosApiError
from test_utils_container import add_container
from test_utils_pool import add_pool


class PunchTest(TestWithServers):
    """
    Simple test to verify the 3 different punch calls.
    :avocado: recursive
    """

    def test_dkey_punch(self):
        """
        The most basic test of the dkey punch function.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=PunchTest,test_dkey_punch
        """
        pool = add_pool(self)
        container = add_container(self, pool)
        container.open()
        try:
            # create an object and write some data into it
            thedata = b"a string that I want to stuff into an object"
            dkey = b"this is the dkey"
            akey = b"this is the akey"
            tx_handle = container.container.get_new_tx()
            self.log.info("Created a new TX for punch dkey test")

            obj = container.container.write_an_obj(
                thedata, len(thedata) + 1, dkey, akey, obj_cls=1, txn=tx_handle)
            self.log.info("Committing the TX for punch dkey test")
            container.container.commit_tx(tx_handle)
            self.log.info("Committed the TX for punch dkey test")

            # read the data back and make sure its correct
            thedata2 = container.container.read_an_obj(
                len(thedata) + 1, dkey, akey, obj, txn=tx_handle)
            if thedata != thedata2.value:
                self.log.info("wrote data: %s", thedata)
                self.log.info("read data:  %s", thedata2.value)
                self.fail("Wrote data, read it back, didn't match\n")

            # now punch this data, should fail, can't punch committed data
            obj.punch_dkeys(tx_handle, [dkey])

            # expecting punch of commit data above to fail
            self.fail("Punch should have failed but it didn't.\n")

        # expecting an exception so do nothing
        except DaosApiError:
            pass

        try:
            container.container.close_tx(tx_handle)
            self.log.info("Closed TX for punch dkey test")

            # now punch this data
            obj.punch_dkeys(0, [dkey])

        # this one should work so error if exception occurs
        except DaosApiError:
            self.fail("Punch should have worked.")

        # there are a bunch of other cases to test here,
        #    --test punching the same updating and punching the same data in
        #    the same tx, should fail
        #    --test non updated data in an open tx, should work
        self.log.info("Test passed")

    def test_akey_punch(self):
        """
        The most basic test of the akey punch function.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=PunchTest,test_akey_punch
        """
        pool = add_pool(self)
        container = add_container(self, pool)
        container.open()
        try:
            # create an object and write some data into it
            dkey = b"this is the dkey"
            data1 = [(b"this is akey 1", b"this is data value 1"),
                     (b"this is akey 2", b"this is data value 2"),
                     (b"this is akey 3", b"this is data value 3")]
            tx_handle = container.container.get_new_tx()
            self.log.info("Created a new TX for punch akey test")
            obj = container.container.write_multi_akeys(
                dkey, data1, obj_cls=1, txn=tx_handle)
            self.log.info("Committing the TX for punch akey test")
            container.container.commit_tx(tx_handle)
            self.log.info("Committed the TX for punch dkey test")

            # read back the 1st epoch's data and check 1 value just to make sure
            # everything is on the up and up
            readbuf = [(data1[0][0], len(data1[0][1]) + 1),
                       (data1[1][0], len(data1[1][1]) + 1),
                       (data1[2][0], len(data1[2][1]) + 1)]
            retrieved_data = container.container.read_multi_akeys(
                dkey, readbuf, obj, txn=tx_handle)
            if retrieved_data[data1[1][0]] != data1[1][1]:
                self.log.info("middle akey: %s", retrieved_data[data1[1][0]])
                self.fail("data retrieval failure")

            # now punch one akey from this data
            obj.punch_akeys(tx_handle, dkey, [data1[1][0]])

            # expecting punch of commit data above to fail
            self.fail("Punch should have failed but it didn't.\n")

        # expecting an exception so do nothing
        except DaosApiError as excep:
            self.log.info(excep)

        try:
            container.container.close_tx(tx_handle)
            self.log.info("Closed TX for punch akey test")

            # now punch the object without a tx
            obj.punch_akeys(0, dkey, [data1[1][0]])

        # expecting it to work this time so error
        except DaosApiError as excep:
            self.fail("Punch should have worked: {}\n".format(excep))

        self.log.info("Test passed")

    def test_obj_punch(self):
        """
        The most basic test of the object punch function.  Really similar
        to above except the whole object is deleted.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=PunchTest,test_obj_punch
        """
        pool = add_pool(self)
        container = add_container(self, pool)
        container.open()
        try:
            # create an object and write some data into it
            thedata = b"a string that I want to stuff into an object"
            dkey = b"this is the dkey"
            akey = b"this is the akey"
            tx_handle = container.container.get_new_tx()
            self.log.info("Created a new TX for punch obj test")
            obj = container.container.write_an_obj(
                thedata, len(thedata) + 1, dkey, akey, obj_cls=1, txn=tx_handle)
            self.log.info("Committing the TX for punch obj test")
            container.container.commit_tx(tx_handle)
            self.log.info("Committed the TX for punch obj test")
            # read the data back and make sure its correct
            thedata2 = container.container.read_an_obj(
                len(thedata) + 1, dkey, akey, obj, txn=tx_handle)
            if thedata != thedata2.value:
                self.log.info("wrote data: %s", thedata)
                self.log.info("read data:  %s", thedata2.value)
                self.fail("Wrote data, read it back, didn't match\n")

            # now punch the object, committed so not expecting it to work
            obj.punch(tx_handle)

            # expecting punch of commit data above to fail
            self.fail("Punch should have failed but it didn't.\n")

        # expecting an exception so do nothing
        except DaosApiError as excep:
            self.log.info(excep)

        try:
            container.container.close_tx(tx_handle)
            self.log.info("Closed TX for punch obj test")

            obj.punch(0)

        # expecting it to work without a tx
        except DaosApiError as excep:
            self.log.info(excep)
            self.fail("Punch should have worked.\n")

        self.log.info("Test passed")
