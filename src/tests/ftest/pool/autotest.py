"""
  (C) Copyright 2018-2023 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from exception_utils import CommandFailure


class PoolAutotestTest(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Tests pool autotest.

    :avocado: recursive
    """

    def test_pool_autotest(self):
        """Test pool autotest.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool,daos_cmd,autotest,quick
        :avocado: tags=PoolAutotestTest,test_pool_autotest
        """
        self.log_step("Create a pool")
        self.add_pool()
        self.pool.set_query_data()
        daos_cmd = self.get_daos_command()

        # Propagate ASAN_OPTIONS to the daos client process when running with
        # ASAN-instrumented RPMs (DAOS-18859 debugging).  If ASAN_OPTIONS is
        # already set in the test environment (e.g. via avocado yaml), use it;
        # otherwise fall back to a sensible default so the ASAN report goes to
        # stderr (captured in daos.log) and halt_on_error surfaces the UAF.
        asan_opts = os.environ.get(
            "ASAN_OPTIONS",
            "halt_on_error=1:atexit=1:leak_check_at_exit=1:"
            "use_sigaltstack=1:detect_odr_violation=0:disable_coredump=1:"
            "handle_segv=2:handle_abort=2:handle_sigfpe=2:"
            "handle_sigill=2:handle_sigbus=2:detect_leaks=1:"
            "max_leaks=100000:print_stats=1")
        daos_cmd.env["ASAN_OPTIONS"] = asan_opts

        self.log_step("Autotest start")
        try:
            daos_cmd.pool_autotest(pool=self.pool.identifier)
            self.log_step("daos pool autotest passed.")
        except CommandFailure as error:
            self.log.error("Error: %s", error)
            self.fail("daos pool autotest failed!")
        finally:
            self.pool.set_query_data()
