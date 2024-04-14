"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from general_utils import report_errors
from recovery_test_base import RecoveryTestBase


class PoolCleanupTest(RecoveryTestBase):
    """Test Pass 3: Pool Cleanup

    :avocado: recursive
    """

    def test_corrupt_label_ms(self):
        """Test corrupt label in MS.

        1. Create a pool.
        2. Mangle the label in the MS (Mangle the copy of the PS metadata exists in the
        MS).
        3. Check that the label in MS is corrupted with -fault added.
        4. Check that the label in PS is not corrupted.
        5. Stop the servers and enable the checker.
        6. Set the policy to --all-interactive.
        7. Start the checker and query the checker until the fault is detected.
        8. Repair with option 1; Trust PS pool label.
        9. Query the checker until the fault is repaired.
        10. Call dmg check disable and restart the servers.
        11. Wait for all ranks to join.
        12. Verify that the corrupted label in MS is fixed.

        Jira ID: DAOS-11741

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,test_cat_recovery,pool_cleanup
        :avocado: tags=PoolCleanupTest,test_corrupt_label_ms
        """
        # 1. Create a pool.
        self.log_step("Create a pool")
        pool = self.get_pool(connect=False)

        # 2. Mangle the label in the MS.
        self.log_step("Mangle the label in the MS")
        dmg_command = self.get_dmg_command()
        dmg_command.faults_mgmt_svc_pool(
            pool=pool.identifier, checker_report_class="CIC_POOL_BAD_LABEL")

        # 3. Check that the label in MS is corrupted with -fault added.
        self.log_step("Check that the label in MS is corrupted with -fault added")
        pool_labels = dmg_command.get_pool_list_labels()
        errors = []
        expected_label = pool.identifier + "-fault"
        if pool_labels[0] != expected_label:
            msg = (f"Unexpected label in MS! Expected = {expected_label}; Actual = "
                   f"{pool_labels[0]}")
            errors.append(msg)

        # 4. Check that the label in PS isn’t corrupted.
        self.log_step("Check that the label in PS isn’t corrupted.")
        # Add -fault to the label and use that to call dmg pool get-prop.
        orig_identifier = pool.identifier
        pool.label.update(orig_identifier + "-fault")
        ps_label = pool.get_property(prop_name="label")
        if ps_label != orig_identifier:
            msg = (f"Unexpected label in PS! Expected = {orig_identifier}; Actual = "
                   f"{ps_label}")
            errors.append(msg)
        # Restore the label to the original.
        pool.label.update(orig_identifier)

        # 5. Stop the servers and enable the checker.
        self.log_step("Stop the servers and enable the checker.")
        dmg_command.check_enable()

        # 6. Set the policy to --all-interactive.
        self.log_step("Set the policy to --all-interactive.")
        dmg_command.check_set_policy(all_interactive=True)

        # 7. Start the checker and query the checker until the fault is detected.
        self.log_step("Start and query the checker until the fault is detected.")
        seq_num = None
        # Start checker.
        dmg_command.check_start()
        # Query the checker until expected number of inconsistencies are repaired.
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            # Status is INIT before starting the checker.
            if check_query_out["response"]["status"] == "RUNNING":
                seq_num = check_query_out["response"]["reports"][0]["seq"]
                break
            time.sleep(5)
        if not seq_num:
            self.fail("Checker didn't detect any fault!")

        # 8. Repair with option 1; Trust PS pool label.
        self.log_step("Repair with option 1; Trust PS pool label (in JSON output).")
        dmg_command.check_repair(seq_num=seq_num, action=1)

        # 9. Query the checker until the fault is repaired.
        self.log_step("Query the checker until the fault is repaired.")
        repair_report = self.wait_for_check_complete()[0]

        # Verify that the repair report has expected message "Update the MS label".
        action_message = repair_report["act_msgs"][0]
        exp_msg = "Update the MS label"
        errors = []
        if exp_msg not in action_message:
            errors.append(f"{exp_msg} not in {action_message}!")

        # 10. Call dmg check disable and restart the servers.
        self.log_step("Call dmg check disable and restart the servers.")
        dmg_command.check_disable()

        # 11. Wait for all ranks to join.
        self.log_step("Wait for all ranks to join.")
        rank_list = self.server_managers[0].get_host_ranks(hosts=self.hostlist_servers)
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=rank_list, valid_states="joined", max_checks=10)
        if failed_ranks:
            errors.append(
                f"Following ranks didn't become joined after restart! {failed_ranks}")

        # 12. Verify that the corrupted label in MS is fixed.
        self.log_step("Verify that the corrupted label in MS is fixed.")
        pool_labels = dmg_command.get_pool_list_labels()
        if pool_labels[0] != pool.identifier:
            msg = (f"Label in MS hasn't been repaired! Expected = "
                   f"{pool.identifier.lower()}; Actual = {pool_labels[0]}")
            errors.append(msg)

        report_errors(test=self, errors=errors)

    def test_corrupt_label_ps(self):
        """Test corrupt label in PS.

        1. Create a pool.
        2. Mangle the label in the PS metadata.
        3. Call dmg pool get-prop TestLabel_1 label and verify that the label value is
        TestLabel_1-fault.
        4. Stop servers and enable the checker.
        5. Set checker policy to --all-interactive.
        6. Start the checker and query the checker until the fault is detected.
        7. Repair with option 0; Trust MS pool label.
        8. Query the checker until the fault is repaired.
        9. Disable the checker and restart servers.
        10. Wait for all ranks to join.
        11. Call dmg pool get-prop mkp1 label and verify that the original label is
        restored.

        Jira ID: DAOS-11742

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,test_cat_recovery,pool_cleanup
        :avocado: tags=PoolCleanupTest,test_corrupt_label_ps
        """
        # 1. Create a pool.
        self.log_step("Create a pool")
        pool = self.get_pool(connect=False)

        # 2. Mangle the label in the PS.
        self.log_step("Mangle the label in the PS")
        dmg_command = self.get_dmg_command()
        dmg_command.faults_pool_svc(
            pool=pool.identifier, checker_report_class="CIC_POOL_BAD_LABEL")

        # 3. Call dmg pool get-prop TestLabel_1 label and verify that the label value is
        # TestLabel_1-fault.
        self.log_step("Check that the label in PS is corrupted with -fault added")
        ps_label = pool.get_property(prop_name="label")
        errors = []
        expected_label = pool.identifier + "-fault"
        if ps_label != expected_label:
            msg = (f"Unexpected label in PS! Expected = {expected_label}; Actual = "
                   f"{ps_label}")
            errors.append(msg)

        # 4. Stop the servers and enable the checker.
        self.log_step("Stop the servers and enable the checker.")
        dmg_command.check_enable()

        # 5. Set the policy to --all-interactive.
        self.log_step("Set the policy to --all-interactive.")
        dmg_command.check_set_policy(all_interactive=True)

        # 6. Start the checker and query the checker until the fault is detected.
        self.log_step("Start and query the checker until the fault is detected.")
        seq_num = None
        # Start checker.
        dmg_command.check_start()
        # Query the checker until the label inconsistency is detected.
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            # Status is INIT before starting the checker.
            if check_query_out["response"]["status"] == "RUNNING":
                seq_num = check_query_out["response"]["reports"][0]["seq"]
                break
            time.sleep(5)
        if not seq_num:
            self.fail("Checker didn't detect any fault!")

        # 7. Repair with option 0; Trust MS pool label.
        self.log_step("Repair with option 0; Trust MS pool label.")
        dmg_command.check_repair(seq_num=seq_num, action=0)

        # 8. Query the checker until the fault is repaired.
        self.log_step("Query the checker until the fault is repaired.")
        repair_report = self.wait_for_check_complete()[0]

        # Verify that the repair report has expected message "Reset the pool property
        # using the MS label".
        action_message = repair_report["act_msgs"][0]
        exp_msg = "Reset the pool property using the MS label"
        errors = []
        if exp_msg not in action_message:
            errors.append(f"{exp_msg} not in {action_message}!")

        # 9. Call dmg check disable and restart the servers.
        self.log_step("Call dmg check disable and restart the servers.")
        dmg_command.check_disable()

        # 10. Wait for all ranks to join.
        self.log_step("Wait for all ranks to join.")
        rank_list = self.server_managers[0].get_host_ranks(hosts=self.hostlist_servers)
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=rank_list, valid_states="joined", max_checks=10)
        if failed_ranks:
            errors.append(
                f"Following ranks didn't become joined after restart! {failed_ranks}")

        # 11. Call dmg pool get-prop TestPool_1 label and verify that the original label
        # is restored in PS.
        self.log_step("Verify that the corrupted label in PS is fixed.")
        ps_label = pool.get_property(prop_name="label")
        if ps_label != pool.identifier:
            msg = (f"Label in PS hasn't been repaired! Expected = "
                   f"{pool.identifier}; Actual = {ps_label}")
            errors.append(msg)

        report_errors(test=self, errors=errors)
