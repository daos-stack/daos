'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback

from apricot import TestWithServers
from general_utils import DaosTestError
from test_utils_container import add_container
from test_utils_pool import add_pool


class ObjFetchBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_fetch function.
    :avocado: recursive
    """

    def initialize_container(self):
        """Initialize a pool and container with data.

        Raises:
            DaosTestError: if there was an error writing the object

        Returns:
            TestContainer: the container initialized with data
        """
        self.log_step('Creating a pool')
        pool = add_pool(self)

        self.log_step('Creating a container')
        container = add_container(self, pool)

        self.log_step('Populating the container with data')
        container.write_objects(obj_class=2)

        return container

    def test_obj_fetch_bad_handle(self):
        """JIRA ID: DAOS-1377

        Test Description: Pass a bogus object handle, should return bad handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjFetchBadParam,test_obj_fetch_bad_handle
        """
        container = self.initialize_container()

        # trash the handle and read again
        self.log_step('Attempt to read the data with a bad object handle, expecting -1002')
        saved_handle = container.written_data[0].obj.obj_handle
        container.written_data[-1].obj.obj_handle = 99999
        try:
            container.read_objects()
            self.fail("Test was expected to return a -1002 with a bad object handle but it has not")
        except DaosTestError as error:
            if '-1002' not in str(error):
                self.log.info(error)
                self.log.info(traceback.format_exc())
                self.fail(
                    "Test was expected to return a -1002 with a bad object handle but it has not")
        finally:
            container.written_data[0].obj = saved_handle

        self.log.info('Test passed')

    def test_null_ptrs(self):
        """JIRA ID: DAOS-1377

        Test Description: Pass null pointers for various fetch parameters.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjFetchBadParam,test_null_ptrs
        """
        container = self.initialize_container()

        self.log_step('Attempt to read the data with a bad dkey, expecting -1003')
        saved_dkey = container.written_data[0].records[0]["dkey"]
        container.written_data[0].records[0]["dkey"] = None
        try:
            container.read_objects()
            self.fail("Test was expected to get -1003 with a bad dkey but it has not.\n")
        except DaosTestError as error:
            if '-1003' not in str(error):
                self.log.info(error)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1003 with a bad dkey but it has not.\n")
        finally:
            container.written_data[0].records[0]["dkey"] = saved_dkey

        # now try it with a null SGL (iod_size is not set)
        self.log_step('Attempt to read the data with a null SGL')
        try:
            container.read_objects(test_hints=['sglnull'])
        except DaosTestError as error:
            self.log.info(error)
            self.log.info(traceback.format_exc())
            self.fail("Test was expected to pass with a null SGL but it failed.\n")

        # now try it with a null iod, expecting this to fail with -1003
        self.log_step('Attempt to read the data with a null iod, expecting -1003')
        try:
            container.read_objects(test_hints=['iodnull'])
            self.fail("Test was expected to get -1003 with a null iod but it has not.\n")
        except DaosTestError as error:
            if '-1003' not in str(error):
                self.log.info(error)
                self.log.info(traceback.format_exc())
                self.fail("Test was expected to get -1003 with a null iod but it has not.\n")

        self.log.info('Test passed')
