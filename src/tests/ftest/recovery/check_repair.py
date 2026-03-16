"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from exception_utils import CommandFailure
from general_utils import report_errors
from recovery_utils import query_detect, wait_for_check_complete


class DMGCheckRepairTest(TestWithServers):
    """Test dmg check repair.

    :avocado: recursive
    """

    def test_check_repair_corner_case(self):
        """Test dmg check repair corner cases.

        1. Create a pool.
        2. Inject orphan pool fault.
        3. Start the checker with interactive mode.
        4. Verify that the orphan pool is detected.
        5. Repair with invalid ID. Verify error message.
        6. Repair with invalid action. Verify error message.
        7. Repair with correct ID and action.
        8. Repair again. Verify error message.

        Jira ID: DAOS-17852

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov
        :avocado: tags=DMGCheckRepairTest,test_check_repair_corner_case
        """
        # 1. Create a pool.
        self.log_step("Create a pool")
        pool = self.get_pool(connect=False)

        # 2. Inject orphan pool fault.
        self.log_step("Inject orphan pool fault")
        dmg_command = self.get_dmg_command()
        dmg_command.faults_mgmt_svc_pool(
            pool=pool.identifier, checker_report_class="CIC_POOL_NONEXIST_ON_MS")

        # 3. Start the checker with interactive mode.
        self.log_step("Start the checker with interactive mode.")
        dmg_command.check_enable()
        dmg_command.check_set_policy(all_interactive=True)
        dmg_command.check_start()

        # 4. Verify that the orphan pool is detected.
        self.log_step("Verify that the orphan pool is detected.")
        query_reports = query_detect(dmg=dmg_command, fault="orphan pool")

        # 5. Repair with invalid ID. Verify error message.
        self.log_step("Repair with invalid ID. Verify error message.")
        # First, get the action value, which is the index of act_msgs.
        # "act_msgs": [
        #    "Re-add the pool",
        #    "Discard the pool",
        #    "Ignore the pool finding"
        # ]
        # We want to select "Re-add the pool", so action is 0.
        act_msgs = query_reports[0]["act_msgs"]
        try:
            action = str(act_msgs.index("Re-add the pool"))
        except ValueError as value_error:
            self.fail(f"Re-add the pool wasn't in the action options! {value_error}")
        invalid_seq_num = "9"
        errors = []
        try:
            dmg_command.check_repair(seq_num=invalid_seq_num, action=action)
        except CommandFailure as command_failure:
            exp_msg = f"finding 0x{invalid_seq_num} not found"
            if exp_msg not in str(command_failure):
                msg = f"Repair with invalid ID didn't return expected message! {exp_msg}"
                errors.append(msg)
            else:
                self.log.info("Repair with invalid ID failed as expected.")

        # 6. Repair with invalid action. Verify error message.
        self.log_step("Repair with invalid action. Verify error message.")
        seq_num = query_reports[0]["seq"]
        invalid_action = "100"
        try:
            dmg_command.check_repair(seq_num=seq_num, action=invalid_action)
        except CommandFailure as command_failure:
            exp_msg = f"invalid action {invalid_action}"
            if exp_msg not in str(command_failure):
                msg = ("Repair with invalid action didn't return expected message! "
                       f"{exp_msg}")
                errors.append(msg)
            else:
                self.log.info("Repair with invalid action failed as expected.")

        # 7. Repair with correct ID and action.
        self.log_step("Repair with correct ID and action.")
        dmg_command.check_repair(seq_num=seq_num, action=action)
        wait_for_check_complete(dmg=dmg_command)

        # 8. Repair again. Verify error message.
        self.log_step("Repair again. Verify error message.")
        try:
            dmg_command.check_repair(seq_num=seq_num, action=action)
        except CommandFailure as command_failure:
            exp_msg = "already resolved"
            if exp_msg not in str(command_failure):
                msg = f"Repeated repair didn't return expected message! {exp_msg}"
                errors.append(msg)
            else:
                self.log.info("Repeated repair failed as expected.")

        # Disable the checker to prepare for the tearDown.
        dmg_command.check_disable()

        report_errors(test=self, errors=errors)
