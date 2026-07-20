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

        # Propagate ASAN_OPTIONS and TSAN_OPTIONS to the daos client process when
        # running with sanitizer-instrumented RPMs (DAOS-18859 debugging).  Both are
        # set unconditionally: whichever sanitizer's runtime is not actually loaded
        # simply ignores its corresponding *_OPTIONS variable, so this works
        # regardless of which sanitizer the RPM was built with.  This process is the
        # DAOS-18859 reproduction target -- the suspected use-after-free is a race
        # between a background CaRT/Mercury TLS thread and the main thread, both
        # inside this process, during pool connect -- so reporting is enabled for
        # both sanitizers here (unlike the other DAOS Go binaries, where TSan
        # reporting is silenced by default to avoid unrelated noise).
        #
        # ASAN: leak detection is disabled (detect_leaks=0) to avoid noise from
        # non-DAOS allocations; UAF/buffer-overflow detection (the main goal) is
        # unaffected.
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
        # TSAN: report_signal_unsafe=0 avoids noise from DAOS's own extensive use of
        # signal handlers for crash/error handling; history_size/second_deadlock_stack
        # give deeper stack traces in any race report.
        daos_cmd.env["TSAN_OPTIONS"] = (
            "halt_on_error=0:"
            "report_signal_unsafe=0:"
            "history_size=7:"
            "second_deadlock_stack=1"
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
