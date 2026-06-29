"""
  (C) Copyright 2018-2023 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
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
        # ASAN-instrumented RPMs (DAOS-18859 debugging).  Leak detection is
        # disabled (detect_leaks=0) to avoid noise from non-DAOS allocations;
        # UAF/buffer-overflow detection (the main goal) is unaffected.
        daos_cmd.env["ASAN_OPTIONS"] = (
            "halt_on_error=0:"
            "atexit=1:"
            "leak_check_at_exit=0:"
            "use_sigaltstack=1:"
            "detect_odr_violation=0:"
            "disable_coredump=1:"
            "handle_segv=2:"
            "handle_abort=2:"
            "handle_sigfpe=2:"
            "handle_sigill=2:"
            "handle_sigbus=2:"
            "detect_leaks=0:"
            "print_stats=0"
        )

        self.log_step("Autotest start")
        try:
            daos_cmd.pool_autotest(pool=self.pool.identifier)
            self.log_step("daos pool autotest passed.")
        except CommandFailure as error:
            self.log.error("Error: %s", error)
            self.fail("daos pool autotest failed!")
        finally:
            self.pool.set_query_data()
