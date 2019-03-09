#!/usr/bin/python
'''
  (C) Copyright 2018 Intel Corporation.

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

import ctypes
import os
import time
import traceback
import sys
import json
import random
import string
from multiprocessing import Process, Manager

from avocado import Test
from avocado import main
from avocado.utils import process

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import ServerUtils
import WriteHostFile
import CheckForPool
import IoUtilities
from daos_api import DaosContext, DaosPool, DaosServer, DaosContainer
from daos_api import DaosApiError

class RebuildWithIO(Test):
    """
    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild.
    """
    def setUp(self):
        self.hostlist = None
        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as f:
            self.build_paths = json.load(f)

        # setup the DAOS python API
        self.Context = DaosContext(self.build_paths['PREFIX'] + '/lib/')

    def tearDown(self):
        pass

    def test_rebuild_with_io(self):
        """
        Test ID: Rebuild-003

        Test Description: Trigger a rebuild while I/O is ongoing.

        Use Cases:
          -- single pool, single client performing continous read/write/verify
             sequence while failure/rebuild is triggered in another process

        :avocado: tags=pool,rebuild,rebuildwithio
        """

        # the rebuild tests need to redo this stuff each time so not in setup
        # as it usually would be
        setid = self.params.get("setname", '/run/testparams/setnames/')
        server_group = self.params.get("name", '/server_config/',
                                          'daos_server')

        basepath = os.path.normpath(self.build_paths['PREFIX']  + "/../")
        tmp = self.build_paths['PREFIX'] + '/tmp'

        self.hostlist = self.params.get("test_machines",'/run/hosts/')
        hostfile = WriteHostFile.WriteHostFile(self.hostlist, tmp)

        io_proc = None

        try:
            ServerUtils.runServer(hostfile, server_group, basepath)

            # use the uid/gid of the user running the test, these should
            # be perfectly valid
            createuid = os.geteuid()
            creategid = os.getegid()

            # parameters used in pool create that are in yaml
            createmode = self.params.get("mode",'/run/testparams/createmode/')
            createsetid = self.params.get("setname",
                                          '/run/testparams/createset/')
            createsize  = self.params.get("size",'/run/testparams/createsize/')

            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(self.Context)
            pool.create(createmode, createuid, creategid,
                        createsize, createsetid, None)
            pool.connect(1 << 1)
            container = DaosContainer(self.Context)
            container.create(pool.handle)
            container.open()

            # get pool status and make sure it all looks good before we start
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
            pool_version = pool.pool_info.pi_rebuild_st.rs_version

            # do I/O for 30 seconds
            bw = IoUtilities.ContinousIo(container, 30)

            # trigger the rebuild
            rank = self.params.get("rank",'/run/testparams/ranks/*')
            server = DaosServer(self.Context, server_group, rank)
            server.kill(1)
            pool.exclude([rank])

            # do another 30 seconds of I/O,
            # waiting for some improvements in server bootstrap
            # at which point we can move the I/O to a separate client and
            # really pound it with I/O
            bw = IoUtilities.ContinousIo(container, 30)

            # wait for the rebuild to finish
            while True:
                pool.pool_query()
                if pool.pool_info.pi_rebuild_st.rs_done == 1:
                    break
                else:
                    time.sleep(2)

            # check rebuild statistics
            if pool.pool_info.pi_ndisabled != 1:
                self.fail("Number of disabled targets reporting incorrectly: {}".
                         format(pool.pool_info.pi_ndisabled))
            if pool.pool_info.pi_rebuild_st.rs_errno != 0:
                self.fail("Rebuild error reported: {}".format(
                    pool.pool_info.pi_rebuild_st.rs_errno))
            if pool.pool_info.pi_rebuild_st.rs_obj_nr <= 0:
                self.fail("No objects have been rebuilt.")
            if pool.pool_info.pi_rebuild_st.rs_rec_nr <= 0:
                self.fail("No records have been rebuilt.")

        except (ValueError, DaosApiError) as e:
                print(e)
                print(traceback.format_exc())
                self.fail("Expecting to pass but test has failed.\n")

        finally:
            # wait for the I/O process to finish
            try:
                ServerUtils.stopServer(hosts=self.hostlist)
                os.remove(hostfile)
                # really make sure everything is gone
                CheckForPool.CleanupPools(self.hostlist)
            finally:
                ServerUtils.killServer(self.hostlist)
