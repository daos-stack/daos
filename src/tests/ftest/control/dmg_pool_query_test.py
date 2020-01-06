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

from dmg_utils import pool_query, get_pool_uuid_from_stdout
from apricot import TestWithServers
from test_utils import TestPool
from avocado.utils import process


class DmgPoolQueryTest(TestWithServers):
    """Test Class Description:
    Simple test to verify the pool query command of dmg tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgPoolQueryTest object."""
        super(DmgPoolQueryTest, self).__init__(*args, **kwargs)
        self.pool = None

    def test_pool_query_basic(self):
        """
        JIRA ID: DAOS-2976
        Test Description: Test basic dmg functionality to query pool info on
        the system.
        :avocado: tags=all,tiny,pr,dmg,pool_query,basic
        """
        self.pool = TestPool(self.context)
        self.pool.get_params(self)
        self.pool.create()
        self.pool.connect(1 << 1)

        # Run dmg pool query command
        dmg_out = pool_query
        dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        dmg.get_params(self)

        # Update hostlist value for dmg command
        port = self.params.get("port", "/run/server_config/*")
        servers_with_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]
        dmg.hostlist.update(",".join(servers_with_ports), "dmg.hostlist")

        try:
            dmg.run()
        except process.CmdError as details:
            self.fail("dmg command failed: {}".format(details))
