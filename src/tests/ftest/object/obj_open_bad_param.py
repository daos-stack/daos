#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

import os
import traceback
from apricot import TestWithServers

from daos_api import DaosPool, DaosContainer, DaosApiError
from daos_cref import DaosObjId

class ObjOpenBadParam(TestWithServers):
    """
    Test Class Description:
    Pass an assortment of bad parameters to the daos_obj_open function.

    :avocado: recursive
    """
    def setUp(self):
        super(ObjOpenBadParam, self).setUp()
        try:
            # parameters used in pool create
            createmode = self.params.get("mode", '/run/pool/createmode/')
            createsetid = self.params.get("setname", '/run/pool/createset/')
            createsize = self.params.get("size", '/run/pool/createsize/')
            createuid = os.geteuid()
            creategid = os.getegid()

            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)

            # need a connection to create container
            self.pool.connect(1 << 1)

            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.handle)

            # now open it
            self.container.open()

            # create an object and write some data into it
            thedata = "a string that I want to stuff into an object"
            self.datasize = len(thedata) + 1
            self.dkey = "this is the dkey"
            self.akey = "this is the akey"
            self.obj, self.epoch = self.container.write_an_obj(thedata,
                                                               self.datasize,
                                                               self.dkey,
                                                               self.akey,
                                                               obj_cls=1)

            thedata2 = self.container.read_an_obj(self.datasize, self.dkey,
                                                  self.akey, self.obj,
                                                  self.epoch)
            if thedata not in thedata2.value:
                print(thedata)
                print(thedata2.value)
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

    def tearDown(self):
        try:
            self.container.close()
            self.container.destroy()
            self.pool.disconnect()
            self.pool.destroy(1)
        finally:
            super(ObjOpenBadParam, self).tearDown()

    def test_bad_obj_handle(self):
        """
        Test ID: DAOS-1320

        Test Description: Attempt to open a garbage object handle.

        :avocado: tags=object,objopen,objopenbadhand,regression,vm,small
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

        :avocado: tags=object,objopen,objopenbadconthand,regression,vm,small
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

        :avocado: tags=object,objopen,objopenclosedcont,regression,vm,small
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

        :avocado: tags=object,objopen,objopenpoolhandle,regression,vm,small
        """
        saved_oh = self.obj.obj_handle
        self.obj.obj_handle = self.pool.handle

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

        :avocado: tags=object,objopen,objopennullrl,regression,vm,small
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

        :avocado: tags=object,objopen,objopennulloid,regression,vm,small
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

        :avocado: tags=object,objopen,objopennulltgts,regression,vm,small
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

        :avocado: tags=object,objopen,objopennullattr,regression,vm,small
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
