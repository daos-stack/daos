"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from osa_utils import OSAUtils
from test_utils_pool import add_pool


class OSADmgNegativeTest(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test passes invalid parameters to dmg OSA related commands and
        check whether appropriate errors are returned.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        # Start an additional server.
        self.extra_servers = self.get_hosts_from_yaml(
            "test_servers", "server_partition", "server_reservation", "/run/extra_servers/*")

        # Dmg command test sequence
        self.test_seq = self.params.get("dmg_cmd_test", "/run/test_sequence/*")

    def validate_results(self, pool, exp_result, result):
        """Validate the result is as expected.

        Args:
            pool (TestPool): the pool to validate
            exp_result (str) : Expected result (Pass or Fail string).
            result (CmdResult) : dmg output.

        Returns:
            bool: where the results validated

        """
        if exp_result == "Pass":
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end(3)
            if result.exit_status == 0:
                self.log.info(
                    "=> '%s' passed as expected (rc=%s)", result.command, result.exit_status)
                return True
        elif exp_result == "Fail":
            if result.exit_status != 0:
                self.log.info(
                    "=> '%s' failed as expected (rc=%s)", result.command, result.exit_status)
                return True
        else:
            self.fail("Invalid 'exp_result' parameter: {}".format(exp_result))
        return False

    def run_osa_dmg_test(self, num_pool, extend=False):
        """Run the offline extend without data.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            extend (bool) : Run testing after performing pool extend.
        """
        # Create a pool
        self.dmg_command.exit_status_exception = False
        pool_list = []

        for index in range(0, num_pool):
            self.log.info("=> Creating pool %s", index + 1)
            pool_list.append(add_pool(self, create=False, connect=False))
            # Split total SCM and NVME size for creating multiple pools.
            pool_list[-1].scm_size.value = int(pool_list[-1].scm_size.value / num_pool)
            pool_list[-1].nvme_size.value = int(pool_list[-1].nvme_size.value / num_pool)
            pool_list[-1].create()

        # Start the additional servers and extend the pool
        if extend is True:
            self.log.info("=> Starting additional servers on %s", self.extra_servers)
            self.start_additional_servers(self.extra_servers)

        # Get rank, target from the test_dmg_sequence
        # Some test_dmg_sequence data will be invalid, valid.
        for pool in pool_list:
            for index, sequence in enumerate(self.test_seq):
                rank = sequence[0]
                target = str(sequence[1])
                expected_result = str(sequence[2])

                # Extend the pool
                # There is no need to extend rank 0
                # Avoid DER_ALREADY
                if extend is True and rank != "0":
                    self.log.info("=> Sequence %s: Extend rank %s onto pool %s", index, rank, pool)
                    if not self.validate_results(pool, expected_result, pool.extend(rank)):
                        self.fail("Error extending rank {} onto pool {}".format(rank, str(pool)))
                if (extend is False and rank in ["4", "5"]):
                    continue

                # Exclude a rank, target
                self.log.info("=> Sequence %s: Exclude rank %s from pool %s", index, rank, pool)
                if not self.validate_results(pool, expected_result, pool.exclude(rank, target)):
                    self.fail("Error excluding rank {} from pool {}".format(rank, str(pool)))

                # Now reintegrate the excluded rank.
                self.log.info("=> Sequence %s: Reintegrate rank %s into pool %s", index, rank, pool)
                if not self.validate_results(pool, expected_result, pool.reintegrate(rank, target)):
                    self.fail("Error reintegrating rank {} into pool {}".format(rank, str(pool)))

                # Drain the data from a rank
                self.log.info("=> Sequence %s: Drain rank %s data from pool %s", index, rank, pool)
                if not self.validate_results(pool, expected_result, pool.drain(rank, target)):
                    self.fail("Error draining rank {} data from pool {}".format(rank, str(pool)))

                # Now reintegrate the drained rank
                self.log.info(
                    "=> Sequence %s: Reintegrate drained rank %s into pool %s", index, rank, pool)
                if not self.validate_results(pool, expected_result, pool.reintegrate(rank, target)):
                    self.fail(
                        "Error reintegrating drained rank {} into pool {}".format(rank, str(pool)))

        self.log.info("Test Passed")

    def test_osa_dmg_cmd_without_extend(self):
        """JIRA ID: DAOS-5866.

        Test Description: Test

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum
        :avocado: tags=OSADmgNegativeTest,dmg_negative_test,test_osa_dmg_cmd_without_extend
        """
        # Perform testing with a single pool
        self.run_osa_dmg_test(1, False)

    def test_osa_dmg_cmd_with_extend(self):
        """JIRA ID: DAOS-5866.

        Test Description: Test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum
        :avocado: tags=OSADmgNegativeTest,dmg_negative_test_extend,test_osa_dmg_cmd_with_extend
        """
        # Perform extend testing with 1 pool
        self.run_osa_dmg_test(1, True)
