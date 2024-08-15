"""
  (C) Copyright 2019-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re

from ior_test_base import IorTestBase


class IorInterceptMessages(IorTestBase):
    """Test class Description: Runs IOR with interception.

       Look for messages provided by the library

    :avocado: recursive
    """

    def test_ior_intercept_messages(self):
        """Jira ID: DAOS-7647.

        Test Description:
            Purpose of this test is to run ior using dfuse with interception
            library enabled and look for some debug messages provided
            by the library.

        Use case:
            Run ior with dfuse flags set on debug
            Look for interception library messages.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,dfuse,ioil,ior,ior_intercept
        :avocado: tags=IorInterceptMessages,test_ior_intercept_messages
        """
        d_il_report_value = self.params.get("value", "/run/tests/D_IL_REPORT/*")
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')

        # D_IL_REPORT VALUES
        #     -1: All printed calls # This needs its own test case
        #      0: Summary on exit
        #      1: Print to stderr the first read call
        #      2: Print to stderr the first 2 read calls
        # If needed the test can mux the value of D_IL_REPORT
        # and look only for a limited number of prints
        self.ior_cmd.env['D_IL_REPORT'] = d_il_report_value

        out = self.run_ior_with_pool(intercept=intercept, fail_on_warning=False)
        stderr = out.stderr.decode("utf-8")

        # Verify expected number of interception messages
        num_intercept = len(re.findall(r"\[libioil\] Intercepting write*", stderr))
        expected = self.processes * int(d_il_report_value)
        if num_intercept != expected:
            self.fail('Expected {} intercept messages but got {}'.format(expected, num_intercept))

        # Verify expected number of summary messages
        num_summary = len(re.findall(
            r"\[libioil\] Performed [0-9]+ reads and [0-9]+ writes from [0-9]+ files*", stderr))
        if num_summary != self.processes:
            self.fail('Expected {} summary messages but got {}'.format(self.processes, num_summary))
