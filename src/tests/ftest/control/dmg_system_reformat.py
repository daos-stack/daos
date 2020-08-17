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
from server_utils import ServerFailed
from command_utils import CommandFailure
from control_test_base import ControlTestBase


class DmgSystemReformatTest(ControlTestBase):
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
        self.add_container(self.pool)

        self.log.info("Stop running io_server instancess: 'dmg system stop'")
        data = self.server_managers[-1].dmg.system_stop()

        # Verify
        if not data:
            self.fail("Detected issues performing a system stop: {}".format(
                self.server_managers[-1].dmg.result.stderr))

        self.log.info("Perform dmg storage format on all system ranks:")
        format_data = self.server_managers[-1].dmg.storage_format(system=True)

        # Verify
        if not format_data:
            self.fail("Detected issues performing storage format: {}".format(
                self.server_managers[-1].dmg.result.stderr))

        # Check that io_servers start up again
        self.server_managers[-1].detect_io_server_start()

        # Check that we have cleared
        pool_info = self.server_managers[-1].dmg.pool_list()
        if pool_info:
            self.fail("Detected pools in storage after refomat: {}".format(
                self.server_managers[-1].dmg.result.stdout))
