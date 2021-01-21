#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

import os

from general_utils import check_file_exists, pcmd
from apricot import TestWithServers


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

        :avocado: tags=all,tiny,daily_regression,ds_versioning,basic
        """
        # Check that the superblock file exists under the scm_mount dir.
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        fname = os.path.join(scm_mount, "superblock")
        check_result = check_file_exists(self.hostlist_servers, fname)
        if not check_result[0]:
            self.fail("{}: {} not found".format(check_result[1], fname))

        # Make sure that 'version' is in the file, run task to check
        cmd = "cat {} | grep -F \"version\"".format(fname)
        result = pcmd(self.hostlist_servers, cmd, timeout=20)

        # Determine if the command completed successfully across all the hosts
        if len(result) > 1 or 0 not in result:
            self.fail("Was not able to find version in {} file".format(fname))
