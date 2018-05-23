#!/usr/bin/python
"""
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
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
import os
import time
import traceback
import sys
import json
import logging

sys.path.append('./util')
import ServerUtils
import CheckForPool
import WriteHostFile

sys.path.append('../../utils/py')
from daos_api import *

from avocado       import Test
from avocado       import main
from avocado.utils import process

progress_log = logging.getLogger("progress")


class InfoTests(Test):
    """
    Tests DAOS pool query.
    """
    def setUp(self):
        with open('../../../.build_vars.json') as f:
            data = json.load(f)
        self.basepath = os.path.normpath(data['PREFIX'] + "/../")
        self.tmp = data['PREFIX'] + '/tmp'
        self.server_group = self.params.get("server_group", '/server/',
                                            'daos_server')

        context = DaosContext(data['PREFIX'] + '/lib/')
        print("initialized!!!\n")

        self.pool = DaosPool(context)
        hostlist = self.params.get("test_machines1", '/run/hosts/')
        hostfile = WriteHostFile.WriteHostFile(hostlist, self.tmp)
        ServerUtils.runServer(hostfile, self.server_group, self.basepath)

    def tearDown(self):
        # shut 'er down
        self.pool.destroy(1)
        ServerUtils.stopServer()
        os.remove(self.hostfile)

    def test_simple_query(self):
        """
        Test querying a pool created on a single server.

        Note tags not set at present because the test isn't in a working state

        :avocado: tags=pool,poolquery,infotest
        """
        # there is a presumption that this test lives in a specific spot
        # in the repo
        setid = self.params.get("setname",
                                '/run/testparams/setnames/validsetname/')

        # create pool
        mode = self.params.get("mode", '/run/testparams/modes/*', 0731)
        uid = os.geteuid()
        gid = os.getegid()
        size = self.params.get("size", '/run/testparams/sizes/*', 0)
        tgt_list = None
        group = self.server_group

        self.pool.create(mode, uid, gid, size, group, tgt_list)
        progress_log.info("created pool")

        # connect to the pool
        flags = self.params.get("perms", '/run/testparams/connectperms/*', '')
        connect_flags = 1 << flags
        self.pool.connect(connect_flags)
        progress_log.info("connected to pool")

        # query the pool
        pool_info = self.pool.pool_query()
        progress_log.info("queried pool info")

        # validate returned pool info, uuid
        uuid_str = ""
        for x in pool_info.pi_uuid:
            uuid_str = uuid_str + ("{0} ".format(x))
        progress_log.info("pool uuid: {0}".format(uuid_str))

        # number of targets
        progress_log.info("number of targets in pool: {0}"
                          .format(pool_info.pi_ntargets))
        if pool_info.pi_ntargets != len(self.hostlist):
            self.fail("found number of targets in pool did not match "
                      "expected number, 1. num targets: {0}"
                      .format(pool_info.pi_ntargets))

        # number of disabled targets
        progress_log.info("number of disabled targets in pool: {0}"
                          .format(pool_info.pi_ndisabled))
        if pool_info.pi_ndisabled > 0:
            self.fail("found disabled targets, none expected to be disabled")

        # mode
        progress_log.info("pool mode: {0}".format(pool_info.pi_mode))
        if pool_info.pi_mode != mode:
            self.fail("found different mode than expected. expected {0}, "
                      "found {1}.".format(mode, pool_info.pi_mode))


if __name__ == "__main__":
    main()
