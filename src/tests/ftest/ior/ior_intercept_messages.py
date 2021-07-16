#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re
import os
from apricot import skipForTicket
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics


class IorInterceptMessages(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR with interception.

       Look for debug messages provided the the library

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
        :avocado: tags=daosio,dfuse,il
        :avocado: tags=iorinterceptmessages
        """
        apis = self.params.get("ior_api", '/run/ior/iorflags/ssf/*')
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        job_manager = self.get_ior_job_manager_command()
        env = self.ior_cmd.get_default_env(str(job_manager), self.client_log)
        env['D_LOG_MASK'] = 'DEBUG'
        env['DD_MASK '] = 'all'
        env['DD_SUBSYS'] = 'all'
        env['LD_PRELOAD'] = intercept
        env['D_IL_REPORT'] = '1'

        match_results = []
        patterns = "^\[libioil\] Performed [0-9]+ reads and [0-9]+ " \
                   "writes from [0-9]+ files*" \
                   "|" \
                   "^\[libioil\] Intercepting write*"

        for api in apis:
            self.ior_cmd.api.update(api)
            if api == "POSIX":
                out = self.run_ior_with_pool(intercept, fail_on_warning=False,
                                             env=env)

                # Check for libioil messages within stderr
                for line in out.stderr.decode("utf-8").splitlines():
                    match = re.findall(patterns, line)
                    if match:
                        match_results.append(match)
                self.assertTrue(match_results, "No libioil messages found.")
