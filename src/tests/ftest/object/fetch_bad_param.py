'''
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback

from apricot import TestWithServers
from pydaos.raw import DaosApiError
from test_utils_container import add_container
from test_utils_pool import add_pool


class ObjFetchBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_fetch function.
    :avocado: recursive
    """

    def create_container(self):
        """Initialize a pool and container.

        Returns:
            TestContainer: the created container
        """
        self.log_step('Creating a pool')
        pool = add_pool(self)

        self.log_step('Creating a container')
        return add_container(self, pool)

    def populate_container(self, container, data, data_size, dkey, akey):
        """Populate a container with data.

        Args:
            container (TestContainer): the container to populate with data

        Returns:
            DaosObj: the object containing the data
        """
        self.log_step('Populating the container with data')
        container.open()
        obj = container.container.write_an_obj(data, data_size, dkey, akey, None, None, 2)
        read = container.container.read_an_obj(data_size, dkey, akey, obj)
        if data not in read.value:
            self.log.info("data: %s", data)
            self.log.info("read: %s", read.value)
            self.fail("Error reading back container data, test failed during the initial setup")
        return obj

    def test_obj_fetch_bad_handle(self):
        """JIRA ID: DAOS-1377

        Test Description: Pass a bogus object handle, should return bad handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjFetchBadParam,test_obj_fetch_bad_handle
        """
        data = b"a string that I want to stuff into an object"
        data_size = len(data) + 1
        dkey = b"this is the dkey"
        akey = b"this is the akey"

        container = self.create_container()
        obj = self.populate_container(container, data, data_size, dkey, akey)
        saved_oh = obj.obj_handle
        try:
            # trash the handle and read again
            obj.obj_handle = 99999

            # expecting this to fail with -1002
            container.container.read_an_obj(data_size, dkey, akey, obj)
            self.fail("Test was expected to return a -1002 but it has not.\n")

        except DaosApiError as error:
            if '-1002' not in str(error):
                self.log.info(error)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1002 but it has not.\n")
        finally:
            container.container.oh = saved_oh

    def test_null_ptrs(self):
        """JIRA ID: DAOS-1377

        Test Description: Pass null pointers for various fetch parameters.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjFetchBadParam,test_null_ptrs
        """
        data = b"a string that I want to stuff into an object"
        data_size = len(data) + 1
        dkey = b"this is the dkey"
        akey = b"this is the akey"

        container = self.create_container()
        obj = self.populate_container(container, data, data_size, dkey, akey)
        try:
            # now try it with a bad dkey, expecting this to fail with -1003
            container.container.read_an_obj(data_size, None, akey, obj)
            self.fail("Test was expected to return a -1003 but it has not.\n")

        except DaosApiError as error:
            if '-1003' not in str(error):
                self.log.info(error)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.\n")

        try:
            # now try it with a null SGL (iod_size is not set)
            test_hints = ['sglnull']
            container.container.read_an_obj(data_size, dkey, akey, obj, test_hints)
        except DaosApiError as error:
            self.log.info(error)
            self.log.info(traceback.format_exc())
            self.fail("Test was expected to pass but failed !\n")

        try:
            # now try it with a null iod, expecting this to fail with -1003
            test_hints = ['iodnull']
            container.container.read_an_obj(data_size, dkey, akey, obj, test_hints)
            self.fail("Test was expected to return a -1003 but it has not.")

        except DaosApiError as error:
            if '-1003' not in str(error):
                self.log.info(error)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1003 but it has not.")
