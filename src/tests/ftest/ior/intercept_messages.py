#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re
import os
from ior_test_base import IorTestBase


class IorInterceptMessages(IorTestBase):
    # pylint: disable=too-many-ancestors
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
        :avocado: tags=hw,small
        :avocado: tags=daosio,dfuse,il,ior_intercept
        :avocado: tags=ior_intercept_messages
        """
        d_il_report_value = self.params.get("value",
                                            "/run/tests/D_IL_REPORT/*")
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        summary_pattern = r"^\[libioil\] Performed [0-9]+ reads and [0-9]+ " \
                          "writes from [0-9]+ files*"
        # Set the env locally for this test
        # Avoiding any impact to the rest of IOR test cases
        job_manager = self.get_ior_job_manager_command()
        env = self.ior_cmd.get_default_env(str(job_manager), self.client_log)
        env['DD_MASK '] = 'all'
        env['DD_SUBSYS'] = 'all'

        # D_IL_REPORT VALUES
        #     -1: All printed calls # This needs its own test case
        #      0: Summary on exit
        #      1: Print to stderr the first read call
        #      2: Print to stderr the first 2 read calls
        # If needed the test can mux the value of D_IL_REPORT
        # and look only for a limited number of prints
        #
        env['D_IL_REPORT'] = d_il_report_value

        # Summary
        match_summary_results = []
        # pylint: disable=anomalous-backslash-in-string

        compiled_sp = re.compile(summary_pattern)
        expected_total_summaries = self.processes

        # Intercept
        match_intercept_results = []
        intercept_pattern = "^\[libioil\] Intercepting write*"
        compiled_ip = re.compile(intercept_pattern)
        expected_total_intercepts = self.processes * int(env['D_IL_REPORT'])

        out = self.run_ior_with_pool(intercept=intercept,
                                     fail_on_warning=False,
                                     env=env)
        # Check for libioil messages within stderr
        for line in out.stderr.decode("utf-8").splitlines():

            # Check for interception messages
            match = compiled_ip.match(line)
            if match:
                match_intercept_results.append(match)
            # Check for summary messages within stderr
            match = compiled_sp.search(line)
            if match:
                match_summary_results.append(match)

        self.assertEqual(len(match_intercept_results),
                         expected_total_intercepts)
        self.assertEqual(len(match_summary_results),
                         expected_total_summaries)
