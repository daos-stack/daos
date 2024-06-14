"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import traceback

from apricot import TestWithServers
from pydaos.raw import DaosApiError, DaosObjId
from test_utils_container import add_container
from test_utils_pool import add_pool


class ObjOpenBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_open function.

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

    def populate_container(self, container):
        """Populate a container with data.

        Args:
            container (TestContainer): the container to populate with data

        Returns:
            DaosObj: the object containing the data
        """
        data = b"a string that I want to stuff into an object"
        data_size = len(data) + 1
        dkey = b"this is the dkey"
        akey = b"this is the akey"

        self.log_step('Populating the container with data')
        container.open()
        obj = container.container.write_an_obj(data, data_size, dkey, akey, obj_cls=1)
        read = container.container.read_an_obj(data_size, dkey, akey, obj)
        if data not in read.value:
            self.log.info("data: %s", data)
            self.log.info("read: %s", read.value)
            self.fail("Error reading back container data, test failed during the initial setup")
        return obj

    def verify_object_open(self, obj, case, code):
        """Attempt to open an object with a bad object handle.

        Args:
            obj (DaosObj): the object to open
            case (str): test case description
            code (str): expected error code
        """
        self.log_step(f'Attempt to open an object {case}, expecting {code}')
        try:
            obj.open()
        except DaosApiError as error:
            if code not in str(error):
                self.d_log.error(f"Object open {case} expecting {code} but not seen")
                self.d_log.error(traceback.format_exc())
                self.fail(f"Object open {case} expecting {code} but not seen")

    def test_bad_obj_handle(self):
        """JIRA ID: DAOS-1320

        Test Description: Attempt to open a garbage object handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjOpenBadParam,test_bad_obj_handle
        """
        container = self.create_container()
        obj = self.populate_container(container)
        saved_handle = obj.obj_handle
        try:
            obj.obj_handle = 8675309
            self.verify_object_open(obj, 'with a garbage object handle', '-1002')
        finally:
            obj.obj_handle = saved_handle
        self.log.info('Test passed')

    def test_invalid_container_handle(self):
        """JIRA ID: DAOS-1320

        Test Description: Attempt to open an object with a garbage container
                          handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjOpenBadParam,test_invalid_container_handle
        """
        container = self.create_container()
        obj = self.populate_container(container)
        saved_coh = container.container.coh
        try:
            container.container.coh = 8675309
            self.verify_object_open(obj, 'with a garbage container handle', '-1002')
        finally:
            container.container.coh = saved_coh
        self.log.info('Test passed')

    def test_closed_container_handle(self):
        """JIRA ID: DAOS-1320

        Test Description: Attempt to open an object in a container with
                          a closed handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjOpenBadParam,test_closed_container_handle
        """
        container = self.create_container()
        obj = self.populate_container(container)
        container.close()
        self.verify_object_open(obj, 'with a closed container handle', '-1002')
        self.log.info('Test passed')

    def test_pool_handle_as_obj_handle(self):
        """Test ID: DAOS-1320

        Test Description: Adding this test by request, this test attempts
                          to open an object that's had its handle set to
                          be the same as a valid pool handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjOpenBadParam,test_pool_handle_as_obj_handle
        """
        container = self.create_container()
        obj = self.populate_container(container)
        saved_handle = obj.obj_handle
        try:
            obj.obj_handle = container.pool.pool.handle
            self.verify_object_open(obj, 'with a object handle matching the pool handle', '-1002')
        finally:
            obj.obj_handle = saved_handle
        self.log.info('Test passed')

    def test_null_ranklist(self):
        """JIRA ID: DAOS-1320

        Test Description: Attempt to open an object in a container with
                          an empty ranklist.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjOpenBadParam,test_null_ranklist
        """
        container = self.create_container()
        obj = self.populate_container(container)
        saved_rank_list = obj.tgt_rank_list
        try:
            obj.tgt_rank_list = None
            self.verify_object_open(obj, 'with a null object rank list', '-1003')
        finally:
            obj.tgt_rank_list = saved_rank_list
        self.log.info('Test passed')

    def test_null_oid(self):
        """JIRA ID: DAOS-1320

        Test Description: Attempt to open an object in a container with
                          null object id.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjOpenBadParam,test_null_oid
        """
        container = self.create_container()
        obj = self.populate_container(container)
        saved_oid = obj.c_oid
        try:
            obj.c_oid = DaosObjId(0, 0)
            self.verify_object_open(obj, 'with a null object id', '-1003')
        finally:
            obj.c_oid = saved_oid
        self.log.info('Test passed')

    def test_null_tgts(self):
        """JIRA ID: DAOS-1320

        Test Description: Attempt to open an object in a container with
                          null tgt.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjOpenBadParam,test_null_tgts
        """
        container = self.create_container()
        obj = self.populate_container(container)
        saved_ctgts = obj.c_tgts
        try:
            obj.c_tgts = 0
            self.verify_object_open(obj, 'in a container with null tgt', '-1003')
        finally:
            obj.c_tgts = saved_ctgts
        self.log.info('Test passed')

    def test_null_attrs(self):
        """JIRA ID: DAOS-1320

        Test Description: Attempt to open an object in a container with
                          null object attributes.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=ObjOpenBadParam,test_null_attrs
        """
        container = self.create_container()
        obj = self.populate_container(container)
        saved_attr = obj.attr
        try:
            obj.attr = 0
            self.verify_object_open(obj, 'in a container with null object attributes', '-1003')
        finally:
            obj.attr = saved_attr
        self.log.info('Test passed')
