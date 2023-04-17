#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


import traceback

from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError, DaosObjId


class ObjOpenBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_open function.

    :avocado: recursive
    """
    def setUp(self):
        super().setUp()
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
                                                   self.akey,
                                                   obj_cls=1)

            thedata2 = self.container.read_an_obj(self.datasize, self.dkey,
                                                  self.akey, self.obj)
            if thedata not in thedata2.value:
                self.log.info("thedata:  %s", thedata)
                self.log.info("thedata2: %s", thedata2.value)
                err_str = "Error reading back data, test failed during the " \
                          "initial setup."
                self.d_log.error(err_str)
                self.fail(err_str)

            # setup leaves object in open state, so closing to start clean
            self.obj.close()

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test failed during the initial setup.")

    def test_bad_obj_handle(self):
        """
        Test ID: DAOS-1320

        Test Description: Attempt to open a garbage object handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=objopenbadhandle,test_bad_obj_handle
        """
        saved_handle = self.obj.obj_handle
        self.obj.obj_handle = 8675309

        try:
            dummy_obj = self.obj.open()
        except DaosApiError as excep:
            if '-1002' not in str(excep):
                self.d_log.error("test expected a -1002 but did not get it")
                self.d_log.error(traceback.format_exc())
                self.fail("test expected a -1002 but did not get it")
        finally:
            self.obj.obj_handle = saved_handle

    def test_invalid_container_handle(self):
        """
        Test ID: DAOS-1320

        Test Description: Attempt to open an object with a garbage container
                          handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=objopenbadcont,test_invalid_container_handle
        """
        saved_coh = self.container.coh
        self.container.coh = 8675309

        try:
            dummy_obj = self.obj.open()
        except DaosApiError as excep:
            if '-1002' not in str(excep):
                self.d_log.error("test expected a -1002 but did not get it")
                self.d_log.error(traceback.format_exc())
                self.fail("test expected a -1002 but did not get it")
        finally:
            self.container.coh = saved_coh

    def test_closed_container_handle(self):
        """
        Test ID: DAOS-1320

        Test Description: Attempt to open an object in a container with
                          a closed handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=objopenclosedcont,test_closed_container_handle
        """
        self.container.close()

        try:
            dummy_obj = self.obj.open()
        except DaosApiError as excep:
            if '-1002' not in str(excep):
                self.d_log.error("test expected a -1002 but did not get it")
                self.d_log.error(traceback.format_exc())
                self.fail("test expected a -1002 but did not get it")
        finally:
            self.container.open()

    def test_pool_handle_as_obj_handle(self):
        """
        Test ID: DAOS-1320

        Test Description: Adding this test by request, this test attempts
                          to open an object that's had its handle set to
                          be the same as a valid pool handle.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=objopenbadpool,test_pool_handle_as_obj_handle
        """
        saved_oh = self.obj.obj_handle
        self.obj.obj_handle = self.pool.pool.handle

        try:
            dummy_obj = self.obj.open()
        except DaosApiError as excep:
            if '-1002' not in str(excep):
                self.d_log.error("test expected a -1002 but did not get it")
                self.d_log.error(traceback.format_exc())
                self.fail("test expected a -1002 but did not get it")
        finally:
            self.obj.obj_handle = saved_oh

    def test_null_ranklist(self):
        """
        Test ID: DAOS-1320

        Test Description: Attempt to open an object in a container with
                          an empty ranklist.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=objopennullrl,test_null_ranklist
        """
        # null rl
        saved_rl = self.obj.tgt_rank_list
        self.obj.tgt_rank_list = None
        try:
            dummy_obj = self.obj.open()
        except DaosApiError as excep:
            if '-1003' not in str(excep):
                self.d_log.error("test expected a -1003 but did not get it")
                self.d_log.error(traceback.format_exc())
                self.fail("test expected a -1003 but did not get it")
        finally:
            self.obj.tgt_rank_list = saved_rl

    def test_null_oid(self):
        """
        Test ID: DAOS-1320

        Test Description: Attempt to open an object in a container with
                          null object id.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=objopennulloid,test_null_oid
        """
        # null oid
        saved_oid = self.obj.c_oid
        self.obj.c_oid = DaosObjId(0, 0)
        try:
            dummy_obj = self.obj.open()
        except DaosApiError as excep:
            if '-1003' not in str(excep):
                self.d_log.error("Test expected a -1003 but did not get it")
                self.d_log.error(traceback.format_exc())
                self.fail("test expected a -1003 but did not get it")
        finally:
            self.obj.c_oid = saved_oid

    def test_null_tgts(self):
        """
        Test ID: DAOS-1320

        Test Description: Attempt to open an object in a container with
                          null tgt.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=objopennulltgts,test_null_tgts
        """
        # null tgts
        saved_ctgts = self.obj.c_tgts
        self.obj.c_tgts = 0
        try:
            dummy_obj = self.obj.open()
        except DaosApiError as excep:
            if '-1003' not in str(excep):
                self.d_log.error("Test expected a -1003 but did not get it")
                self.d_log.error(traceback.format_exc())
                self.fail("test expected a -1003 but did not get it")
        finally:
            self.obj.c_tgts = saved_ctgts

    def test_null_attrs(self):
        """
        Test ID: DAOS-1320

        Test Description: Attempt to open an object in a container with
                          null object attributes.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=object
        :avocado: tags=objopennullattr,test_null_attrs
        """
        # null attr
        saved_attr = self.obj.attr
        self.obj.attr = 0
        try:
            dummy_obj = self.obj.open()
        except DaosApiError as excep:
            if '-1003' not in str(excep):
                self.d_log.error("test expected a -1003 but did not get it")
                self.d_log.error(traceback.format_exc())
                self.fail("test expected a -1003 but did not get it")
        finally:
            self.obj.attr = saved_attr
