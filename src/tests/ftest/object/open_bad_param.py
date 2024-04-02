"""
  (C) Copyright 2018-2023 Intel Corporation.

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
        container = self.initialize_container()
        saved_handle = container.written_data[0].obj.obj_handle
        container.written_data[0].obj.obj_handle = 8675309
        try:
            self.verify_object_open(
                container.written_data[0].obj, 'with a garbage object handle', '-1002')
        finally:
            container.written_data[0].obj.obj_handle = saved_handle
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
        container = self.initialize_container()
        saved_coh = container.container.coh
        container.container.coh = 8675309
        try:
            self.verify_object_open(
                container.written_data[0].obj, 'with a garbage container handle', '-1002')
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
        container = self.initialize_container()
        container.close()
        try:
            self.verify_object_open(
                container.written_data[0].obj, 'with a closed container handle', '-1002')
        finally:
            container.open()
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
        container = self.initialize_container()
        saved_handle = container.written_data[0].obj.obj_handle
        container.written_data[0].obj.obj_handle = container.pool.pool.handle
        try:
            self.verify_object_open(
                container.written_data[0].obj, 'with a object handle matching the pool handle',
                '-1002')
        finally:
            container.written_data[0].obj.obj_handle = saved_handle
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
        container = self.initialize_container()
        saved_rank_list = container.written_data[0].obj.tgt_rank_list
        container.written_data[0].obj.tgt_rank_list = None
        try:
            self.verify_object_open(
                container.written_data[0].obj, 'with a null object rank list', '-1003')
        finally:
            container.written_data[0].obj.tgt_rank_list = saved_rank_list
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
        container = self.initialize_container()
        saved_oid = container.written_data[0].obj.c_oid
        container.written_data[0].obj.c_oid = DaosObjId(0, 0)
        try:
            self.verify_object_open(
                container.written_data[0].obj, 'with a null object id', '-1003')
        finally:
            container.written_data[0].obj.c_oid = saved_oid
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
        container = self.initialize_container()
        saved_ctgts = container.written_data[0].obj.c_tgts
        container.written_data[0].obj.c_tgts = 0
        try:
            self.verify_object_open(
                container.written_data[0].obj, 'in a container with null tgt', '-1003')
        finally:
            container.written_data[0].obj.c_tgts = saved_ctgts
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
        container = self.initialize_container()
        saved_attr = container.written_data[0].obj.attr
        container.written_data[0].obj.attr = 0
        try:
            self.verify_object_open(
                container.written_data[0].obj, 'in a container with null object attributes',
                '-1003')
        finally:
            container.written_data[0].obj.attr = saved_attr
        self.log.info('Test passed')
