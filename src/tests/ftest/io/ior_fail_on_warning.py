#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

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

import traceback
from avocado.core.exceptions import TestFail
from ior_test_base import IorTestBase
from ior_utils import IorCommand


class IorFailOnWarning(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR on a single server and single
       client setting with fail_on_warning parameter.

    :avocado: recursive
    """

    def test_ior_fail_on_warning(self):
        """Jira ID: DAOS-5435.

        Test Description:
            Purpose of this test is to run ior using dfuse for 10 seconds
            forcing WARNING messages in order to test the fail_on_warning
            parameter.

        Use case:
            Run ior with read, write, CheckWrite, CheckRead for 10 seconds

        :avocado: tags=all,full_regression,hw,small,daosio,iorfailonwarning
        """
        try:
            # IOR command should fail if 'WARNING' found.
            self.run_ior_with_pool(fail_on_warning=True)
        except TestFail as exc:
            self.log.info(exc)
            self.log.info(traceback.format_exc())
            self.log.info("==> Test expected to Fail. Test PASSED")

        # IOR command should succeed, even when 'WARNING' found.
        self.run_ior_with_pool()
