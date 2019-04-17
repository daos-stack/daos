#!/usr/bin/python
'''
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
  The Governments rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

import os
import time
import traceback
import random
import string
from apricot import TestWithoutServers


import server_utils
import check_for_pool
from daos_api import DaosPool, DaosServer, DaosContainer
from daos_api import DaosApiError

class RebuildTests(TestWithoutServers):

    """
    Test Class Description:
    This class contains tests for pool rebuild.
    :avocado: recursive
    """

    def setUp(self):
        super(RebuildTests, self).setUp()
        self.createuid = os.geteuid()
        self.creategid = os.getegid()
        # parameters used in pool create that are in yaml
        self.createmode = self.params.get("mode", '/run/testparams/createmode/')
        self.createsetid = self.params.get("setname",
                                           '/run/testparams/createset/')
        self.createsize = self.params.get("size",
                                          '/run/testparams/createsize/')
        # how many objects and records are we creating
        self.objcount = self.params.get("objcount",
                                        '/run/testparams/numobjects/*')
        self.reccount = self.params.get("reccount",
                                        '/run/testparams/numrecords/*')
        if self.objcount == 0:
            self.reccount = 0
        # which rank to write to and kill
        self.rank = self.params.get("rank", '/run/testparams/ranks/*')
        # how much data to write with each key
        self.size = self.params.get("size", '/run/testparams/datasize/*')
        #Start the server
        self.server_group = self.params.get("server_group", '/server/',
                                            'daos_server')

    def tearDown(self):
        try:
            # really make sure everything is gone
            check_for_pool.cleanup_pools(self.hostlist_servers)
        finally:
            super(RebuildTests, self).tearDown()

    def test_simple_rebuild(self):
        """
        Test ID: Rebuild-001

        Test Description: The most basic rebuild test.

        Use Cases:
          -- single pool rebuild, single client, various reord/object
             counts

        :avocado: tags=pool,rebuild,rebuildsimple
        """
        try:

            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(self.context)
            pool.create(self.createmode, self.createuid, self.creategid,
                        self.createsize, self.createsetid)

            # want an open connection during rebuild
            pool.connect(1 << 1)

            # get pool status we want to test later
            pool.pool_query()
            if pool.pool_info.pi_ndisabled != 0:
                self.fail("Number of disabled targets reporting incorrectly.\n")
            if pool.pool_info.pi_rebuild_st.rs_errno != 0:
                self.fail("Rebuild error but rebuild hasn't run.\n")
            if pool.pool_info.pi_rebuild_st.rs_done != 1:
                self.fail("Rebuild is running but device hasn't failed yet.\n")
            if pool.pool_info.pi_rebuild_st.rs_obj_nr != 0:
                self.fail("Rebuilt objs not zero.\n")
            if pool.pool_info.pi_rebuild_st.rs_rec_nr != 0:
                self.fail("Rebuilt recs not zero.\n")

            # create a container
            container = DaosContainer(self.context)
            container.create(pool.handle)

            # now open it
            container.open()

            saved_data = []
            for _objc in range(self.objcount):
                obj = None
                for _recc in range(self.reccount):
                    # make some stuff up and write
                    dkey = (
                        ''.join(random.choice(string.ascii_uppercase +
                                              string.digits) for _ in range(5)))
                    akey = (
                        ''.join(random.choice(string.ascii_uppercase +
                                              string.digits) for _ in range(5)))
                    data = (''.join(random.choice(string.ascii_uppercase +
                                                  string.digits)
                                    for _ in range(self.size)))

                    obj, txn = container.write_an_obj(data, len(data), dkey,
                                                      akey, obj, self.rank,
                                                      obj_cls=16)

                    saved_data.append((obj, dkey, akey, data, txn))

                    # read the data back and make sure its correct
                    data2 = container.read_an_obj(self.size, dkey, akey, obj,
                                                  txn)
                    if data != data2.value:
                        self.fail("Write data 1, read it back, didn't match\n")

            # kill a server that has
            server = DaosServer(self.context, self.server_group, self.rank)
            server.kill(1)

            # temporarily, the exclude of a failed target must be done manually
            pool.exclude([self.rank])

            while True:
                # get the pool/rebuild status again
                pool.pool_query()
                if pool.pool_info.pi_rebuild_st.rs_done == 1:
                    break
                else:
                    time.sleep(2)

            if pool.pool_info.pi_ndisabled != 1:
                self.fail("Number of disabled targets reporting incorrectly: {}"
                          .format(pool.pool_info.pi_ndisabled))
            if pool.pool_info.pi_rebuild_st.rs_errno != 0:
                self.fail("Rebuild error reported: {}"
                          .format(pool.pool_info.pi_rebuild_st.rs_errno))
            if pool.pool_info.pi_rebuild_st.rs_obj_nr != self.objcount:
                self.fail("Rebuilt objs not as expected: {0} {1}"
                          .format(pool.pool_info.pi_rebuild_st.rs_obj_nr,
                                  self.objcount))
            if (pool.pool_info.pi_rebuild_st.rs_rec_nr !=
                    (self.reccount*self.objcount)):
                self.fail("Rebuilt recs not as expected: {0} {1}"
                          .format(pool.pool_info.pi_rebuild_st.rs_rec_nr,
                                  self.reccount*self.objcount))

            # now that the rebuild finished verify the records are correct
            for tup in saved_data:
                data2 = container.read_an_obj(len(tup[3]), tup[1], tup[2],
                                              tup[0], tup[4])
                if tup[3] != data2.value:
                    self.fail("after rebuild data didn't check out")

        except DaosApiError as excp:
            print (excp)
            print (traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")

    def test_multipool_rebuild(self):
        """
        Test ID: Rebuild-002
        Test Description: Expand on the basic test by rebuilding 2
        pools at once.

        Use Cases:
          -- multipool rebuild, single client, various object and record counds

        :avocado: tags=pool,rebuild,rebuildmulti
        """
        try:
            # initialize python pool object then create the underlying
            # daos storage, the way the code is now the pools should be
            # on the same storage and have the same service leader
            pool1 = DaosPool(self.context)
            pool2 = DaosPool(self.context)
            pool1.create(self.createmode, self.createuid, self.creategid,
                         self.createsize, self.createsetid)
            pool2.create(self.createmode, self.createuid, self.creategid,
                         self.createsize, self.createsetid)

            # want an open connection during rebuild
            pool1.connect(1 << 1)
            pool2.connect(1 << 1)

            # create containers
            container1 = DaosContainer(self.context)
            container1.create(pool1.handle)
            container2 = DaosContainer(self.context)
            container2.create(pool2.handle)

            # now open them
            container1.open()
            container2.open()

            # Putting the same data in both pools, at least for now to simplify
            # checking its correct
            saved_data = []
            for _objc in range(self.objcount):
                obj = None
                for _recc in range(self.reccount):

                    # make some stuff up and write
                    dkey = (
                        ''.join(random.choice(string.ascii_uppercase +
                                              string.digits) for _ in range(5)))
                    akey = (
                        ''.join(random.choice(string.ascii_uppercase +
                                              string.digits) for _ in range(5)))
                    data = (
                        ''.join(random.choice(string.ascii_uppercase +
                                              string.digits) for _ in
                                range(self.size)))

                    # Used DAOS_OC_R1S_SPEC_RANK
                    # 1 replica with specified rank
                    obj, txn = container1.write_an_obj(data, len(data), dkey,
                                                       akey, obj, self.rank,
                                                       obj_cls=15)
                    obj, txn = container2.write_an_obj(data, len(data), dkey,
                                                       akey, obj, self.rank,
                                                       obj_cls=15)
                    saved_data.append((obj, dkey, akey, data, txn))

                    # read the data back and make sure its correct containers
                    data2 = container1.read_an_obj(self.size, dkey, akey, obj,
                                                   txn)
                    if data != data2.value:
                        self.fail("Wrote data P1, read it back, didn't match\n")
                    data2 = container2.read_an_obj(self.size, dkey, akey, obj,
                                                   txn)
                    if data != data2.value:
                        self.fail("Wrote data P2, read it back, didn't match\n")

            # kill a server
            server = DaosServer(self.context, self.server_group, self.rank)
            server.kill(1)

            # temporarily, the exclude of a failed target must be done
            # manually
            pool1.exclude([self.rank])
            pool2.exclude([self.rank])

            # check that rebuild finishes, no errors, progress data as
            # know it to be.  Check pool 1 first then we'll check 2 below.
            while True:
                pool1.pool_query()
                if pool1.pool_info.pi_rebuild_st.rs_done == 1:
                    break
                else:
                    time.sleep(2)

            # check there are no errors and other data matches what we
            # apriori know to be true,
            if pool1.pool_info.pi_ndisabled != 1:
                self.fail("P1 number disabled targets reporting incorrectly: {}"
                          .format(pool1.pool_info.pi_ndisabled))
            if pool1.pool_info.pi_rebuild_st.rs_errno != 0:
                self.fail("P1 rebuild error reported: {}"
                          .format(pool1.pool_info.pi_rebuild_st.rs_errno))
            if pool1.pool_info.pi_rebuild_st.rs_obj_nr != self.objcount:
                self.fail("P1 rebuilt objs not as expected: {0} {1}"
                          .format(pool1.pool_info.pi_rebuild_st.rs_obj_nr,
                                  self.objcount))
            if (pool1.pool_info.pi_rebuild_st.rs_rec_nr !=
                    (self.reccount*self.objcount)):
                self.fail("P1 rebuilt recs not as expected: {0} {1}"
                          .format(pool1.pool_info.pi_rebuild_st.rs_rec_nr,
                                  self.reccount*self.objcount))

            # now that the rebuild finished verify the records are correct
            for tup in saved_data:
                data2 = container1.read_an_obj(len(tup[3]), tup[1], tup[2],
                                               tup[0], tup[4])
                if tup[3] != data2.value:
                    self.fail("after rebuild data didn't check out")

            # now check the other pool
            while True:
                pool2.pool_query()
                if pool2.pool_info.pi_rebuild_st.rs_done == 1:
                    break
                else:
                    time.sleep(2)

            # check there are no errors and other data matches what we
            # apriori know to be true
            if pool2.pool_info.pi_ndisabled != 1:
                self.fail("Number disabled targets reporting incorrectly: {}"
                          .format(pool2.pool_info.pi_ndisabled))
            if pool2.pool_info.pi_rebuild_st.rs_errno != 0:
                self.fail("Rebuild error reported: {}"
                          .format(pool2.pool_info.pi_rebuild_st.rs_errno))
            if pool2.pool_info.pi_rebuild_st.rs_obj_nr != self.objcount:
                self.fail("Rebuilt objs not as expected: {0} {1}"
                          .format(pool2.pool_info.pi_rebuild_st.rs_obj_nr,
                                  self.objcount))
            if (pool2.pool_info.pi_rebuild_st.rs_rec_nr !=
                    (self.reccount*self.objcount)):
                self.fail("Rebuilt recs not as expected: {0} {1}".
                          format(pool2.pool_info.pi_rebuild_st.rs_rec_nr,
                                 (self.reccount*self.objcount)))

            # now that the rebuild finished verify the records are correct
            for tup in saved_data:
                data2 = container2.read_an_obj(len(tup[3]), tup[1], tup[2],
                                               tup[0], tup[4])
                if tup[3] != data2.value:
                    self.fail("after rebuild data didn't check out")

        except DaosApiError as excp:
            print (excp)
            print (traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")

        finally:
            server_utils.stop_server(hosts=self.hostlist_servers)
            check_for_pool.cleanup_pools(self.hostlist_servers)
            server_utils.kill_server(self.hostlist_servers)
