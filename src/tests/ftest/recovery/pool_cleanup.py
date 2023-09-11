"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from general_utils import report_errors


class PoolCleanupTest(TestWithServers):
    """Test Pass 3: Pool Cleanup

    :avocado: recursive
    """

    def wait_for_check_complete(self):
        """Repeatedly call dmg check query until status becomes COMPLETED.

        If the status doesn't become COMPLETED, fail the test.

        Returns:
            list: List of repair reports.

        """
        repair_reports = None
        for _ in range(8):
            check_query_out = self.get_dmg_command().check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                repair_reports = check_query_out["response"]["reports"]
                break
            time.sleep(5)

        if not repair_reports:
            self.fail("Checker didn't detect or repair any inconsistency!")

        return repair_reports

    def test_corrupt_label_ms(self):
        """Test corrupt label in MS.

        1. Create a pool.
        2. Mangle the label in the MS (Mangle the copy of the PS metadata exists in the
        MS).
        3. Check that the label in MS is corrupted with -fault added.
        4. Check that the label in PS isn’t corrupted.
        5. Stop the servers and enable the checker.
        6. Set the policy to --all-interactive.
        7. Start the checker and query the checker until the fault is detected.
        8. Repair with option 1; Trust PS pool label.
        9. Query the checker until the fault is repaired.
        10. Call dmg check disable and restart the servers.
        11. Wait for all ranks to join.
        12. Verify that the corrupted label in MS is fixed.

        Jira ID: DAOS-11741

        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=recovery,pool_cleanup
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

        # Verify that the repair report has expected message.
        action_message = repair_report["act_msgs"][0]
        EXP_MSG = "Update the MS label"
        errors = []
        if EXP_MSG not in action_message:
            errors.append(f"{EXP_MSG} not in {action_message}!")

        # 10. Call dmg check disable and restart the servers.
        self.log_step("Call dmg check disable and restart the servers.")
        dmg_command.check_disable()

        # 11. Wait for all ranks to join.
        self.log_step("Wait for all ranks to join.")
        rank_list = self.server_managers[0].get_host_ranks(hosts=self.hostlist_servers)
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=rank_list, valid_states="joined", max_checks=10)
        if failed_ranks:
            self.fail(
                f"Following ranks didn't become joined after restart! {failed_ranks}")

        # 12. Verify that the corrupted label in MS is fixed.
        self.log_step("Verify that the corrupted label in MS is fixed.")
        pool_labels = dmg_command.get_pool_list_labels()
        if pool_labels[0] != pool.identifier:
            msg = (f"Label in MS hasn't been repaired! Expected = "
                   f"{pool.identifier.lower()}; Actual = {pool_labels[0]}")
            errors.append(msg)

        report_errors(test=self, errors=errors)
