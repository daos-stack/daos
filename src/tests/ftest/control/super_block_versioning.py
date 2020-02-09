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

from general_utils import check_file_exists
from apricot import TestWithServers


class SuperBlockVersioning(TestWithServers):
    """Test Class Description:
    Test to verify that super block data structure is versioned.
    :avocado: recursive
    """

    def test_super_block_version_basic(self):
        """
        JIRA ID: DAOS-2895
        Test Description: Basic test to verify that superblock file is
        versioned.
        :avocado: tags=all,tiny,pr,ds_versioning,basic
        """
        # Check that the superblock file exists under the scm_mount dir.
        params = self.server_managers[-1].runner.job.yaml_params
        fname = os.path.join(
            params.server_params[-1].scm_mount.value, "superblock")
        check_result = check_file_exists(self.hostlist_servers, fname)
        if not check_result[0]:
            self.fail("%s: %s not found", check_result[1], fname)

        # Make sure that 'version' is in the file.
        ver = False
        fp = open(fname, 'r')
        for line in fp.readlines():
            if "version" in line:
                ver = True
                self.log.info(
                    "Found version in superblock file: %s", line.split()[1])

        if not ver:
            self.fail("Was not able to find version in {} file".format(fname))
