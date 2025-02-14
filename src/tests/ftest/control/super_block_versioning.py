"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from command_utils import command_as_user
from general_utils import check_file_exists
from run_utils import run_remote


class SuperBlockVersioning(TestWithServers):
    """Superblock data structure test cases.

    Test Class Description:
        Test to verify that super block data structure is versioned.

    :avocado: recursive
    """

    def test_super_block_version_basic(self):
        """JIRA ID: DAOS-3648.

        Test Description:
            Basic test to verify that superblock file is versioned.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,basic
        :avocado: tags=SuperBlockVersioning,test_super_block_version_basic
        """
        # Check that the superblock file exists under the scm_mount dir.
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        fname = os.path.join(scm_mount, "superblock")
        check_result = check_file_exists(self.hostlist_servers, fname, sudo=True)
        if not check_result[0]:
            self.fail("{}: {} not found".format(check_result[1], fname))

        # Make sure that 'version' is in the file, run task to check
        cmd = command_as_user(f'cat {fname} | grep -F "version"', "root")
        result = run_remote(self.log, self.hostlist_servers, cmd, timeout=20)
        if not result.passed:
            self.fail(f"Was not able to find version in {fname} file")
