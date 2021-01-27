#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import print_function

import time
import traceback

from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError


class ObjFetchBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_fetch function.
    :avocado: recursive
    """
    def setUp(self):
        super(ObjFetchBadParam, self).setUp()
        time.sleep(5)

        self.prepare_pool()

        try:
            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.pool.handle)

            # now open it
            self.container.open()

            # create an object and write some data into it
            thedata = "a string that I want to stuff into an object"
            self.datasize = len(thedata) + 1
            self.dkey = "this is the dkey"
            self.akey = "this is the akey"
            self.obj = self.container.write_an_obj(thedata,
                                                   self.datasize,
                                                   self.dkey,
                                                   self.akey, None,
                                                   None, 2)

            thedata2 = self.container.read_an_obj(self.datasize, self.dkey,
                                                  self.akey, self.obj)
            if thedata not in thedata2.value:
                print(thedata)
                print(thedata2.value)
                self.fail("Error reading back data, test failed during"\
                         " the initial setup.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test failed during the initial setup.\n")

    def test_bad_handle(self):
        """
        Test ID: DAOS-1377

        Test Description: Pass a bogus object handle, should return bad handle.

        :avocado: tags=all,object,full_regression,small,objbadhandle
        """

        try:
            # trash the handle and read again
            saved_oh = self.obj.obj_handle
            self.obj.obj_handle = 99999

            # expecting this to fail with -1002
            dummy_thedata2 = self.container.read_an_obj(self.datasize,
                                                        self.dkey, self.akey,
                                                        self.obj)

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

        :avocado: tags=all,object,full_regression,small,objfetchnull
        """
        try:
            # now try it with a bad dkey, expecting this to fail with -1003
            dummy_thedata2 = self.container.read_an_obj(self.datasize, None,
                                                        self.akey, self.obj)

            self.container.close()
            self.container.destroy()
            self.pool.destroy(1)
            self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")

        try:
            # now try it with a null sgl (iod_size is not set)
            test_hints = ['sglnull']
            dummy_thedata2 = self.container.read_an_obj(self.datasize,
                                                        self.dkey, self.akey,
                                                        self.obj, test_hints)
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test was expected to pass but failed !\n")

        try:
            # when DAOS-1449 is complete, uncomment and retest
            # now try it with a null iod, expecting this to fail with -1003
            #test_hints = ['iodnull']
            #thedata2 = self.container.read_an_obj(self.datasize, dkey, akey,
            #                                 self.obj, test_hints)
            pass
            #self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")
