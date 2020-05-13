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

from general_utils import check_file_exists
from pydaos.raw import IORequest
from apricot import TestWithServers


class SCMConfigTest(TestWithServers):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Simple test to verify the SCM storage config.
    :avocado: recursive
    """

    def test_scm_in_use_basic(self):
        """
        JIRA ID: DAOS-2972

        Test Description: Verify that an attempt to configure devices that have
        already been configured and are in use by DAOS is handled.

        :avocado: tags=all,small,pr,hw,scm_in_use,basic
        """
        # Check that pmem is mounted
        self.log.info(
            "==>    Verifying pmem is mounted: <%s>", self.hostlist_servers)
        if self.server_managers[-1].manager.job.using_dcpm:
            scm_list = self.server_managers[-1].get_config_value("scm_list")
            errors = []
            for pmem in scm_list:
                check_result = check_file_exists(self.hostlist_servers, pmem)
                if not check_result[0]:
                    errors.append("{}: {}".format(pmem, check_result[1]))
            if errors:
                self.fail("pmems not found: \n{}".format(errors))
        else:
            self.fail("Detected dcpm not specified")

        # Storage prepare should return error
        self.log.info("==>    Verifying storage prepare is done")
        self.server_managers[-1].dmg.exit_status_exception = False

        result = self.server_managers[-1].dmg.storage_prepare(scm=True)
        if result.exit_status == 0:
            self.fail("Storage prepare expected to fail: {}".format(
                result.stdout))

    def test_scm_mock_media_error(self):
        """
        JIRA ID: DAOS-2972

        Test Description: Using ipmctl tool, inject an error into DIMMs to
        verify that we handle cases of bad devices.

        :avocado: tags=all,small,full_regression,hw,scm_media_error,basic
        """
