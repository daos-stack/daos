#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from osa_utils import OSAUtils
from test_utils_pool import TestPool


class OSADmgNegativeTest(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test
    passes invalid parameters to dmg OSA
    related commands and check whether
    appropriate errors are returned.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        # Start an additional server.
        self.extra_servers = self.params.get("test_servers",
                                             "/run/extra_servers/*")
        # Dmg command test sequence
        self.test_seq = self.params.get("dmg_cmd_test",
                                        "/run/test_sequence/*")

    def validate_results(self, exp_result, dmg_output):
        """Validate the dmg_output results with expected results

        Args:
            exp_result (str) : Expected result (Pass or Fail string).
            dmg_output (str) : dmg output string.
        """
        if exp_result == "Pass":
            # Check state before hand as wait for rebuild
            # does not consider the idle state
            state = self.get_rebuild_state()
            if state not in ("done", "idle"):
                self.is_rebuild_done(3)
            if "succeeded" in dmg_output:
                self.log.info("Test Passed")
            else:
                self.fail("Test Failed")
        elif exp_result == "Fail":
            if "failed" in dmg_output:
                self.log.info("Test Passed")
            else:
                self.fail("Test Failed")
        else:
            self.fail("Invalid Parameter")

    def run_osa_dmg_test(self, num_pool, extend=False):
        """Run the offline extend without data.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            extend (bool) : Run testing after performing pool extend.
        """
        # Create a pool
        self.dmg_command.exit_status_exception = False
        pool = {}
        pool_uuid = []

        for val in range(0, num_pool):
            pool[val] = TestPool(
                context=self.context, dmg_command=self.dmg_command,
                label_generator=self.label_generator)
            pool[val].get_params(self)
            # Split total SCM and NVME size for creating multiple pools.
            pool[val].scm_size.value = int(pool[val].scm_size.value /
                                           num_pool)
            pool[val].nvme_size.value = int(pool[val].nvme_size.value /
                                            num_pool)
            pool[val].create()
            pool_uuid.append(pool[val].uuid)
            self.pool = pool[val]

        # Start the additional servers and extend the pool
        if extend is True:
            self.log.info("Extra Servers = %s", self.extra_servers)
            self.start_additional_servers(self.extra_servers)

        # Get rank, target from the test_dmg_sequence
        # Some test_dmg_sequence data will be invalid, valid.
        for val in range(0, num_pool):
            for i in range(len(self.test_seq)):
                self.pool = pool[val]
                rank = self.test_seq[i][0]
                target = "{}".format(self.test_seq[i][1])
                expected_result = "{}".format(self.test_seq[i][2])
                # Extend the pool
                # There is no need to extend rank 0
                # Avoid DER_ALREADY
                if extend is True and rank != "0":
                    output = self.dmg_command.pool_extend(self.pool.uuid, rank)
                    self.log.info(output)
                    self.validate_results(expected_result, output.stdout_text)
                if (extend is False and rank in ["4","5"]):
                    continue
                # Exclude a rank, target
                output = self.dmg_command.pool_exclude(self.pool.uuid,
                                                       rank,
                                                       target)
                self.log.info(output)
                self.validate_results(expected_result, output.stdout_text)
                # Now reintegrate the excluded rank.
                output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                           rank,
                                                           target)
                self.log.info(output)
                self.validate_results(expected_result, output.stdout_text)
                # Drain the data from a rank
                output = self.dmg_command.pool_drain(self.pool.uuid,
                                                     rank,
                                                     target)
                self.log.info(output)
                self.validate_results(expected_result, output.stdout_text)
                # Now reintegrate the drained rank
                output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                           rank,
                                                           target)
                self.log.info(output)
                self.validate_results(expected_result, output.stdout_text)

    def test_osa_dmg_cmd_without_extend(self):
        """
        JIRA ID: DAOS-5866

        Test Description: Test

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa,checksum
        :avocado: tags=osa_dmg_negative_test,dmg_negative_test
        """
        # Perform testing with a single pool
        self.run_osa_dmg_test(1, False)

    def test_osa_dmg_cmd_with_extend(self):
        """
        JIRA ID: DAOS-5866

        Test Description: Test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa,checksum
        :avocado: tags=osa_dmg_negative_test,dmg_negative_test_extend
        """
        # Perform extend testing with 1 pool
        self.run_osa_dmg_test(1, True)
