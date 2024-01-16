"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re

from ior_test_base import IorTestBase


class IorInterceptMessagesPil4dfs(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR with interception.

       Look for messages provided by the library

    :avocado: recursive
    """

    def test_ior_intercept_messages_pil4dfs(self):
        """Jira ID: DAOS-12142.

        Test Description:
            Purpose of this test is to run ior using dfuse with interception
            library libpil4dfs enabled and look for some debug messages provided
            by the library to make sure IL is loaded.

        Use case:
            Run ior with dfuse flags set on debug
            Look for interception library messages.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,dfuse,il,ior,ior_intercept,pil4dfs
        :avocado: tags=IorInterceptMessagesPil4dfs,test_ior_intercept_messages_pil4dfs
        """
        intercept = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
        self.ior_cmd.env['D_IL_REPORT'] = "true"

        out = self.run_ior_with_pool(intercept=intercept, fail_on_warning=False)
        stderr = out.stderr.decode("utf-8")

        # Verify expected number of interception messages
        num_intercept = len(re.findall(r"\[write  \]  100", stderr))
        expected = self.processes * 1
        if num_intercept != expected:
            self.fail('Expected {} intercept messages but got {}'.format(expected, num_intercept))
