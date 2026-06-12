"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class PoolAutotestEqPollFITest(TestWithServers):
    """Test daos pool autotest robustness under daos_eq_poll() fault injection.

    Validates the DAOS-19016 fix: the kv_put() and kv_get() spin loops in
    src/utils/daos_autotest.c must handle daos_eq_poll() returning a negative
    error code without dereferencing the stale event pointer (evp).

    Fault injection point DAOS_FAULT_EQ_POLL_FAIL (ID 135168) injects a
    -DER_HG return from daos_eq_poll(), exercising the rc < 0 break added by
    the fix.  The expected outcome is:
      - daos pool autotest exits with rc == 1 (no crash or hang)
      - the error message contains DER_HG(-1020)

    :avocado: recursive
    """

    def test_pool_autotest_eq_poll_fi(self):
        """Test that daos pool autotest handles daos_eq_poll() errors correctly.

        Run daos pool autotest with fault injection point DAOS_FAULT_EQ_POLL_FAIL
        (fault ID 135168, enabled via the YAML faults section) active.  Confirm
        that when daos_eq_poll() returns -DER_HG the autotest exits cleanly with
        rc == 1 and reports DER_HG(-1020), proving that the stale event pointer
        fix from DAOS-19016 is working.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool,daos_cmd,autotest,fault_injection
        :avocado: tags=test_pool_autotest_eq_poll_fi,PoolAutotestEqPollFITest
        """
        self.log_step("Create a pool")
        self.add_pool()
        self.pool.set_query_data()
        daos_cmd = self.get_daos_command()

        # Fault injection is enabled via the YAML 'fault_list' section.
        # The autotest is expected to fail: disable the exception so that the
        # CmdResult can be inspected for the expected error signature.
        self.log_step("Run pool autotest with daos_eq_poll fault injection (DAOS-19016)")
        daos_cmd.exit_status_exception = False
        result = daos_cmd.pool_autotest(pool=self.pool.identifier)

        self.log_step("Verify autotest exited with the expected error code")
        if result.exit_status == 0:
            self.fail(
                "daos pool autotest succeeded unexpectedly; "
                "expected it to fail due to DAOS_FAULT_EQ_POLL_FAIL injection")
        if result.exit_status != 1:
            self.fail(
                f"Expected exit code 1, got {result.exit_status}; "
                f"stderr: {result.stderr_text}")

        self.log_step("Verify DER_HG(-1020) error in autotest output")
        if "DER_HG(-1020)" not in result.stderr_text:
            self.fail(
                f"Expected 'DER_HG(-1020)' in autotest stderr; "
                f"got: {result.stderr_text}")
        self.log.info(
            "Fault injection correctly propagated DER_HG(-1020) "
            "without stale event pointer dereference")

        self.log_step("Confirm pool is still healthy after the expected autotest failure")
        self.pool.set_query_data()
