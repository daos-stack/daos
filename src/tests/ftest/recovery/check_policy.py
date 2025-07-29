"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from exception_utils import CommandFailure
from general_utils import report_errors
from recovery_test_base import RecoveryTestBase


class DMGCheckPolicyTest(RecoveryTestBase):
    """Test checker policy functionality.

    :avocado: recursive
    """

    def check_all_default(self, dmg_command):
        """Check all policies are default.

        Args:
            dmg_command (DmgCommand): DmgCommand object to call check get-policy.

        Returns:
            list: List of class name and its policy. e.g., "POOL_NONEXIST_ON_MS:DEFAULT"
        """
        policy_out = dmg_command.check_get_policy()
        policies = policy_out["response"]["policies"]
        for policy in policies:
            current_policy = policy.split(":")[1]
            if current_policy != "DEFAULT":
                class_name = policy.split(":")[0]
                self.fail(f"Policy for {class_name} isn't DEFAULT! {current_policy}")
        return policies

    def check_interact(self, dmg_command, interact_count):
        """Check the first interact_count policies are INTERACT.

        Args:
            dmg_command (DmgCommand): DmgCommand object to call check get-policy.
            interact_count (int): Number of policies to check whether it's INTERACT.
        """
        policy_out = dmg_command.check_get_policy()
        policies = policy_out["response"]["policies"]
        # Check the first interact_count policies are INTERACT.
        for i in range(interact_count):
            policy = policies[i].split(":")[1]
            if policy != "INTERACT":
                class_name = policies[i].split(":")[0]
                msg = (f"Unexpected policy for {class_name}! Expected = INTERACT, "
                       f"Actual = {policy}")
                self.fail(msg)
        # Check the rest of the policies are DEFAULT.
        for i in range(interact_count, len(policies)):
            policy = policies[i].split(":")[1]
            if policy != "DEFAULT":
                class_name = policies[i].split(":")[0]
                msg = (f"Unexpected policy for {class_name}! Expected = DEFAULT, "
                       f"Actual = {policy}")
                self.fail(msg)

    def test_check_policies(self):
        """Test checker policy functionality.

        1. Create a pool.
        2. Inject orphan pool fault.
        3. Enable checker. Verify that all policies are initially DEFAULT.
        4. Set invalid policy to some policy. Verify error message.
        5. Set CIA_INTERACT to some policies.
        6. Set CIA_INTERACT to one of the policies from the previous step.
        7. Verify that INTERACT is set to the policies from the previous step. Also
        verify that the rest of the policies are still DEFAULT.
        8. Disable and enable. Verify that the same policies are still there.
        9. Reset with --reset-defaults. Verify that all policies are back to DEFAULT.
        10. Start the checker. It should automatically repair the fault. Verify that the
        fault is fixed by check query and pool query output.

        Jira ID: DAOS-17706

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=recovery,cat_recov
        :avocado: tags=DMGCheckPolicyTest,test_check_policies
        """
        # 1. Create a pool.
        self.log_step("Create a pool")
        pool = self.get_pool(connect=False)

        # 2. Inject orphan pool fault.
        self.log_step("Inject orphan pool fault")
        dmg_command = self.get_dmg_command()
        dmg_command.faults_mgmt_svc_pool(
            pool=pool.identifier, checker_report_class="CIC_POOL_NONEXIST_ON_MS")

        # 3. Enable checker. Verify that all policies are initially DEFAULT.
        self.log_step("Enable checker. Verify that all policies are initially DEFAULT.")
        dmg_command.check_enable()
        policies = self.check_all_default(dmg_command=dmg_command)

        # 4. Set invalid policy to some policy. Verify error message.
        self.log_step("Set invalid policy to some policy. Verify error message.")
        class_name = "POOL_NONEXIST_ON_MS"
        invalid_policy = "CIA_X"
        invalid_class_policy = class_name + ":" + invalid_policy
        errors = []
        try:
            dmg_command.check_set_policy(policies=invalid_class_policy)
        except CommandFailure as command_failure:
            exp_msg = f'invalid inconsistency action "{invalid_policy}"'
            if exp_msg not in str(command_failure):
                msg = f"Setting invalid policy didn't return expected message! {exp_msg}"
                errors.append(msg)
            else:
                self.log.info("Setting invalid policy failed as expected.")

        # 5. Set CIA_INTERACT to some policies.
        self.log_step("Set CIA_INTERACT to some policies.")
        interact_count = 4
        for i in range(interact_count):
            class_name = policies[i].split(":")[0]
            class_policy = class_name + ":" + "CIA_INTERACT"
            dmg_command.check_set_policy(policies=class_policy)

        # 6. Set CIA_INTERACT to one of the policies from the previous step.
        self.log_step("Set CIA_INTERACT to one of the policies from the previous step.")
        class_name = policies[0].split(":")[0]
        class_policy = class_name + ":" + "CIA_INTERACT"
        dmg_command.check_set_policy(policies=class_policy)

        # 7. Verify that INTERACT is set to the policies from the previous step. Also
        # verify that the rest of the policies are still DEFAULT.
        msg = ("Verify that INTERACT is set to the policies from the previous step. "
               "Also verify that the rest of the policies are still DEFAULT.")
        self.log_step(msg)
        self.check_interact(dmg_command=dmg_command, interact_count=interact_count)

        # 8. Disable and enable. Verify that the same policies are still there.
        self.log_step(
            "Disable and enable. Verify that the same policies are still there.")
        dmg_command.check_disable()
        dmg_command.check_enable()
        self.check_interact(dmg_command=dmg_command, interact_count=interact_count)

        # 9. Reset with --reset-defaults. Verify that all policies are back to DEFAULT.
        msg = "Reset with --reset-defaults. Verify that all policies are back to DEFAULT."
        self.log_step(msg)
        dmg_command.check_set_policy(reset_defaults=True)
        self.check_all_default(dmg_command=dmg_command)

        # 10. Start the checker. It should automatically repair the fault.
        self.log_step("Start the checker. It should automatically repair the fault.")
        dmg_command.check_start()
        # Verify that the orphan pool is fixed by querying the checker.
        self.wait_for_check_complete()
        dmg_command.check_disable()
        # Verify that the orphan pool is fixed by calling dmg pool query. If the fault
        # isn't fixed, the command would fail. Directly call pool_query() instead of
        # calling pool.query() because catching the failure is easier this way.
        try:
            dmg_command.pool_query(pool=pool.uuid)
            self.log.info("dmg pool query worked as expected.")
        except CommandFailure as command_failure:
            self.fail(
                f"Pool query failed after fault is fixed by checker! {command_failure}")

        report_errors(test=self, errors=errors)
