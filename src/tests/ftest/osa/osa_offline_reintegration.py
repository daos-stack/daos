#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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
import time
from apricot import TestWithServers
from test_utils_pool import TestPool


class OSAOfflineReintegration(TestWithServers):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline reintegration test cases.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super(OSAOfflineReintegration, self).setUp()
        self.dmg_command = self.get_dmg_command()

    def get_pool_version(self):
        out = []
        kwargs = {"pool_uuid": self.pool.uuid}
        out = self.dmg_command.get_output("pool_query", **kwargs)
        pool_version = out[0][4]
        pool_version = pool_version.split('=')
        return int(pool_version[1])

    def test_osa_offline_reintegration(self):
        """Test ID: DAOS-4749
        Test Description: Validate Offline Reintegration

        :avocado: tags=all,hw,large,osa,pr
        :avocado: tags=offline_reintegration
        """
        # Create a pool
        self.pool = TestPool(self.context, dmg_command=self.dmg_command)
        self.pool.get_params(self)
        self.pool.create()
        self.pool.display_pool_daos_space("Pool space at the Beginning")
        pversion_begin = self.get_pool_version()
        self.log.info("Pool Version at the beginning %d", pversion_begin)
        # Exclude a rank 2
        output = self.dmg_command.pool_exclude(self.pool.uuid, 2, "2,4")
        self.log.info(output)
        time.sleep(5)
        pversion_exclude = self.get_pool_version()
        self.log.info("Pool Version at the beginning %d", pversion_exclude)
        output = self.dmg_command.pool_reintegrate(self.pool.uuid, 2, "2,4")
        self.log.info(output)
        time.sleep(5)
        pversion_reintegrate = self.get_pool_version()
        self.log.info("Pool Version at the beginning %d", pversion_reintegrate)
