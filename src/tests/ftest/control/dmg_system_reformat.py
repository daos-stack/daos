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
from __future__ import print_function

from avocado import fail_on
from apricot import TestWithServers
from daos_utils import DaosCommand
from server_utils import ServerFailed
from command_utils import CommandFailure


class DmgSystemReformatTest(TestWithServers):
    # pylint: disable=too-many-ancestors
    """Test Class Description:

    Test to verify that dmg storage system format command reformat option.

    :avocado: recursive
    """

    @fail_on(CommandFailure)
    @fail_on(ServerFailed)
    def test_dmg_system_reformat(self):
        """
        JIRA ID: DAOS-5415

        Test Description: Test dmg system reformat functionality.

        :avocado: tags=all,small,pr,hw,control,sys_reformat,dmg
        """

        # Create pool and container
        self.prepare_pool()
        self.add_container(pool=self.pool, daos_command=DaosCommand(self.bin))

        self.log.info("Stop running io_server instancess: 'dmg system stop'")
        data = self.server_managers[-1].dmg.system_stop(force=True)

        # Verify
        if not data:
            self.fail("Detected issues performing a system stop: {}".format(
                self.server_managers[-1].dmg.result.stderr))

        self.log.info("Perform dmg storage format on all system ranks:")
        format_data = self.server_managers[-1].dmg.storage_format(reformat=True)

        # Verify
        if not format_data:
            self.fail("Detected issues performing storage format: {}".format(
                self.server_managers[-1].dmg.result.stderr))

        # Check that io_servers starts up again
        self.log.info("<SERVER> Waiting for the daos_io_servers to start")
        self.server_managers[-1].manager.job.pattern_count = 2
        if not self.server_managers[-1].manager.job.\
           check_subprocess_status(self.server_managers[-1].manager.process):
            self.server_managers[-1].kill()
            raise ServerFailed("Failed to start servers after format")

        # Check that we have cleared
        pool_info = self.server_managers[-1].dmg.pool_list()
        if pool_info:
            self.fail("Detected pools in storage after refomat: {}".format(
                self.server_managers[-1].dmg.result.stdout))

        # Remove pools and containers since they were wiped from memory
        self.pool = None
        self.container = None
