#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import print_function

import time
import traceback

from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError, c_uuid_to_str


class BasicTxTest(TestWithServers):
    """
    A very simple test verifying the use of transactions.  This test was
    butchered in the transition from epoch to transaction.  Need to revisit
    and make more useful when transactions are fully working.
    :avocado: recursive
    """

    def test_tx_basics(self):
        """
        Perform I/O to an object in a container in 2 different transactions,
        verifying basic I/O and transactions in particular.

        NOTE: this was an epoch test and all I did was get it working with tx
        Not a good test at this point, need to redesign when tx is fully
        working.

        :avocado: tags=all,container,tx,small,smoke,daily_regression,basictx
        """
        # initialize a python pool object then create the underlying
        # daos storage and connect to the pool
        self.prepare_pool()

        try:
            # create a container
            container = DaosContainer(self.context)
            container.create(self.pool.pool.handle)

            # now open it
            container.open()

            # do a query and compare the UUID returned from create with
            # that returned by query
            container.query()

            if container.get_uuid_str() != c_uuid_to_str(
                    container.info.ci_uuid):
                self.fail("Container UUID did not match the one in info\n")

            # create an object and write some data into it
            thedata = "a string that I want to stuff into an object"
            thedatasize = 45
            dkey = "this is the dkey"
            akey = "this is the akey"

            oid = container.write_an_obj(thedata, thedatasize,
                                         dkey, akey, None, None, 2)

            # read the data back and make sure its correct
            thedata2 = container.read_an_obj(thedatasize, dkey, akey,
                                             oid)
            if thedata != thedata2.value:
                print("thedata>" + thedata)
                print("thedata2>" + thedata2.value)
                self.fail("Write data 1, read it back, didn't match\n")

            # repeat above, but know that the write_an_obj call is advancing
            # the epoch so the original copy remains and the new copy is in
            # a new epoch.
            thedata3 = "a different string"
            thedatasize2 = 19
            # note using the same keys so writing to the same spot
            dkey = "this is the dkey"
            akey = "this is the akey"

            oid = container.write_an_obj(thedata3, thedatasize2,
                                         dkey, akey, oid, None, 2)

            # read the data back and make sure its correct
            thedata4 = container.read_an_obj(thedatasize2, dkey, akey,
                                             oid)
            if thedata3 != thedata4.value:
                self.fail("Write data 2, read it back, didn't match\n")

            # transactions generally don't work this way but need to explore
            # an alternative to below code once model is complete, maybe
            # read from a snapshot or read from TX_NONE etc.

            # the original data should still be there too
            #thedata5 = container.read_an_obj(thedatasize, dkey, akey,
            #                                 oid, transaction)
            #if thedata != thedata5.value:
            #    self.fail("Write data 3, read it back, didn't match\n")

            container.close()

            # wait a few seconds and then destroy
            time.sleep(5)
            container.destroy()

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test was expected to pass but it failed.\n")
