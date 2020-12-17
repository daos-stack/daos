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
