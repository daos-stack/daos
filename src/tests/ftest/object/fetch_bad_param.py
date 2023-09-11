'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time
import traceback

from pydaos.raw import DaosContainer, DaosApiError

from apricot import TestWithServers


class ObjFetchBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_fetch function.
    :avocado: recursive
    """
    def setUp(self):
        super().setUp()
        time.sleep(5)

        self.prepare_pool()

        try:
            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.pool.handle)

            # now open it
            self.container.open()

            # create an object and write some data into it
            thedata = b"a string that I want to stuff into an object"
            self.datasize = len(thedata) + 1
            self.dkey = b"this is the dkey"
            self.akey = b"this is the akey"
            self.obj = self.container.write_an_obj(thedata,
                                                   self.datasize,
                                                   self.dkey,
                                                   self.akey, None,
                                                   None, 2)

            thedata2 = self.container.read_an_obj(self.datasize, self.dkey,
                                                  self.akey, self.obj)
            if thedata not in thedata2.value:
                self.log.info("thedata:  %s", thedata)
                self.log.info("thedata2: %s", thedata2.value)
                self.fail(
                    "Error reading back data, test failed during the initial "
                    "setup.\n")

        except DaosApiError as excep:
            self.log.info(excep)
            self.log.info(traceback.format_exc())
            self.fail("Test failed during the initial setup.\n")

    def test_obj_fetch_bad_handle(self):
        """JIRA ID: DAOS-1377

        Test Description: Pass a bogus object handle, should return bad handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjFetchBadParam,test_obj_fetch_bad_handle
        """
        try:
            # trash the handle and read again
            saved_oh = self.obj.obj_handle
            self.obj.obj_handle = 99999

            # expecting this to fail with -1002
            self.container.read_an_obj(self.datasize, self.dkey, self.akey, self.obj)

            self.container.oh = saved_oh
            self.fail("Test was expected to return a -1002 but it has not.\n")

        except DaosApiError as excep:
            self.container.oh = saved_oh
            if '-1002' not in str(excep):
                self.log.info(excep)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1002 but it has not.\n")

    def test_null_ptrs(self):
        """JIRA ID: DAOS-1377

        Test Description: Pass null pointers for various fetch parameters.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjFetchBadParam,test_null_ptrs
        """
        try:
            # now try it with a bad dkey, expecting this to fail with -1003
            self.container.read_an_obj(self.datasize, None, self.akey, self.obj)

            self.container.close()
            self.container.destroy()
            self.pool.destroy(1)
            self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                self.log.info(excep)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")

        try:
            # now try it with a null SGL (iod_size is not set)
            test_hints = ['sglnull']
            self.container.read_an_obj(self.datasize, self.dkey, self.akey, self.obj, test_hints)
        except DaosApiError as excep:
            self.log.info(excep)
            self.log.info(traceback.format_exc())
            self.fail("Test was expected to pass but failed !\n")

        try:
            # now try it with a null iod, expecting this to fail with -1003
            test_hints = ['iodnull']
            self.container.read_an_obj(self.datasize, self.dkey, self.akey, self.obj, test_hints)
            self.fail("Test was expected to return a -1003 but it has not.")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                self.log.info(excep)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.")
