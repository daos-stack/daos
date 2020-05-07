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

from command_utils import CommandFailure
from server_utils import ServerFailed
from control_test_base import ControlTestBase
from general_utils import pcmd


class DmgStorageReformatTest(ControlTestBase):
    """Test Class Description:
    Test to verify that dmg storage command reformat option.
    :avocado: recursive
    """

    def test_dmg_storage_reformat(self):
        """
        JIRA ID: DAOS-3854
        Test Description: Test dmg storage reformat functionality.
        :avocado: tags=all,small,full_regression,hw,control,reformat,dmg,basic
        """

        # At this point the server has been started, storage has been formatted
        # We need to get the superblock file information
        errors = []
        scm_mount = self.server_managers[-1].get_config_value("scm_mount")
        orig_uuid = self.get_superblock_info(scm_mount, "uuid")

        # Stop servers
        errors.extend(self.stop_servers())
        if errors:
            self.fail("Errors detected stopping servers:\n  - {}".format(
                "\n  - ".join(errors)))

        # Remove the superblock file from the servers
        cmd = "rm -rf {}/superblock".format(scm_mount)
        pcmd(self.hostlist_servers, cmd, timeout=20)

        # Start servers again
        self.log.info("==>    STARTING SERVERS")
        self.server_managers[-1].prepare()
        self.detect_format_ready()

        # Disable throwing dmg failure here since we expect it to fail
        self.server_managers[-1].dmg.exit_status_exception = False
        self.log.info("==>    Formatting hosts: <%s>", self.dmg.hostlist)
        self.server_managers[-1].dmg.storage_format()

        self.log.info("==>    Waiting for the servers to ask for reformat.")
        self.server_managers[-1].manager.job.update_pattern(
            "reformat", len(self.server_managers[-1]._hosts))

        if not self.server_managers[-1].manager.job.check_subprocess_status(
                self.server_managers[-1].manager.process):
            self.fail("Failed to detect reformat mode.")

        self.log.info("==>    Executing reformat command")
        self.server_managers[-1].dmg.exit_status_exception = True

        try:
            self.server_managers[-1].dmg.storage_format(reformat=True)
        except CommandFailure as error:
            self.fail("Failure to reformat storage: {}".format(error))

        # Check that io-servers have started
        try:
            self.server_managers[-1].detect_io_server_start()
        except ServerFailed as error:
            self.fail("Failed starting io_servers after reformat: {}".format(
                error))

        # Get new superblock uuid
        new_uuid = self.get_superblock_info(scm_mount, "uuid")

        # Verify that old and new uuids are different
        if orig_uuid == new_uuid:
            self.fail("Old and new UUIDs are not unique.")
