#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import print_function

import traceback
import logging

from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError


class ObjUpdateBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_update function.
    :avocado: recursive
    """
    def setUp(self):
        super(ObjUpdateBadParam, self).setUp()
        self.plog = logging.getLogger("progress")
        try:
            self.prepare_pool()
            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.pool.handle)
            self.plog.info("Container %s created.",
			                        self.container.get_uuid_str())
            # now open it
            self.container.open()
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test failed during setup .\n")

    def test_bad_handle(self):
        """
        Test ID: DAOS-1376

        Test Description: Pass a bogus object handle, should return bad handle.

        :avocado: tags=all,object,full_regression,small,objbadhand
        """

        try:
            # create an object and write some data into it
            thedata = "a string that I want to stuff into an object"
            thedatasize = len(thedata) + 1
            dkey = "this is the dkey"
            akey = "this is the akey"
            obj = self.container.write_an_obj(thedata, thedatasize,
                                         dkey, akey, None, None, 2)

            saved_oh = obj.obj_handle
            obj.obj_handle = 99999

            obj = self.container.write_an_obj(thedata, thedatasize,
                                         dkey, akey, obj, None, 2)

            self.container.oh = saved_oh
            self.fail("Test was expected to return a -1002 but it has not.\n")
        except DaosApiError as excep:
            self.container.oh = saved_oh
            self.plog.info("Test Complete")
            if '-1002' not in str(excep):
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -1002 but it has not.\n")

    def test_null_values(self):
        """
        Test ID: DAOS-1376

        Test Description: Pass a dkey and an akey that is null.

        :avocado: tags=all,object,full_regression,small,objupdatenull
        """

        # data used in the test
        thedata = "a string that I want to stuff into an object"
        thedatasize = len(thedata) + 1

        try:
            # try using a null dkey
            dkey = None
            akey = "this is the akey"

            self.container.write_an_obj(thedata, thedatasize, dkey, akey, None,
                                   None, 2)
            self.plog.error("Didn't get expected return code.")
            self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                self.plog.error("Didn't get expected return code.")
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")

        try:
            # try using a null akey/io descriptor
            dkey = "this is the dkey"
            akey = None
            self.container.write_an_obj(thedata, thedatasize, dkey, akey, None,
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

            self.container.write_an_obj(thedata, thedatasize, dkey, akey, None,
                                   None, 2)
            self.plog.info("Update with no data worked")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.plog.error("Update with no data failed")
            self.fail("Update with no data failed.\n")

        self.plog.info("Test Complete")
