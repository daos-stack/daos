'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback

from apricot import TestWithServers
from pydaos.raw import DaosApiError
from test_utils_container import add_container
from test_utils_pool import add_pool


class ObjUpdateBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_update function.
    :avocado: recursive
    """

    def test_obj_update_bad_handle(self):
        """
        Test ID: DAOS-1376

        Test Description: Pass a bogus object handle, should return bad handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjUpdateBadParam,test_obj_update_bad_handle
        """
        pool = add_pool(self)
        container = add_container(self, pool)
        container.open()
        try:
            # create an object and write some data into it
            thedata = b"a string that I want to stuff into an object"
            thedatasize = len(thedata) + 1
            dkey = b"this is the dkey"
            akey = b"this is the akey"
            obj = container.container.write_an_obj(
                thedata, thedatasize, dkey, akey, None, None, 2)

            saved_oh = obj.obj_handle
            obj.obj_handle = 99999

            obj = container.container.write_an_obj(
                thedata, thedatasize, dkey, akey, obj, None, 2)

            container.container.oh = saved_oh
            self.fail("Test was expected to return a -1002 but it has not.\n")
        except DaosApiError as excep:
            container.container.oh = saved_oh
            self.log.info("Test Complete")
            if '-1002' not in str(excep):
                self.log.info(excep)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1002 but it has not.\n")

        self.log.info("Test passed")

    def test_null_values(self):
        """
        Test ID: DAOS-1376

        Test Description: Pass a dkey and an akey that is null.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjUpdateBadParam,test_null_values
        """
        pool = add_pool(self)
        container = add_container(self, pool)
        container.open()

        # data used in the test
        thedata = b"a string that I want to stuff into an object"
        thedatasize = len(thedata) + 1

        try:
            # try using a null dkey
            dkey = None
            akey = b"this is the akey"

            container.container.write_an_obj(
                thedata, thedatasize, dkey, akey, None, None, 2)
            self.log.error("Didn't get expected return code.")
            self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                self.log.error("Didn't get expected return code.")
                self.log.info(excep)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")

        try:
            # try using a null akey/io descriptor
            dkey = b"this is the dkey"
            akey = None
            container.container.write_an_obj(
                thedata, thedatasize, dkey, akey, None, None, 2)
            self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as excep:
            if '-1003' not in str(excep):
                self.log.error("Didn't get expected return code.")
                self.log.info(excep)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")

        try:
            # lastly try passing no data
            thedata = None
            thedatasize = 0
            dkey = b"this is the dkey"
            akey = b"this is the akey"

            container.container.write_an_obj(
                thedata, thedatasize, dkey, akey, None, None, 2)
            self.log.info("Update with no data worked")

        except DaosApiError as excep:
            self.log.info(excep)
            self.log.info(traceback.format_exc())
            self.log.error("Update with no data failed")
            self.fail("Update with no data failed.\n")

        self.log.info("Test passed")
