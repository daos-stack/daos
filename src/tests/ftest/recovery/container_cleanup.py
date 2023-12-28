"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from exception_utils import CommandFailure
from general_utils import report_errors
from recovery_test_base import RecoveryTestBase


class ContainerCleanupTest(RecoveryTestBase):
    """Test Pass 5: Container Cleanup

    :avocado: recursive
    """

    def test_container_label_inconsistency(self):
        """Test container label inconsistency in CS and property.

        1. Create a pool and a container.
        2. Inject fault to cause container label inconsistency. i.e., Corrupt label in CS.
        3. Try to access property using the original label in CS.
        4. Show that PS has the original label by using "new-label", which was injected by
        the fault injection tool.
        5. Enable the checker.
        6. Set policy to --all-interactive.
        7. Start the checker and query the checker until the fault is detected.
        8. Repair by selecting "Trust the container label in container property."
        9. Query the checker until the fault is repaired.
        10. Disable the checker.
        11. Verify that the inconsistency was fixed.

        Jira ID: DAOS-12289

        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=recovery,container_cleanup
        :avocado: tags=ContainerCleanupTest,test_container_label_inconsistency
        """
        # 1. Create a pool and a container.
        self.log_step("Create a pool and a container")
        pool = self.get_pool(connect=False)
        container = self.get_container(pool=pool)

        # 2. Inject fault to cause container label inconsistency.
        self.log_step("Inject fault to cause container label inconsistency.")
        daos_command = self.get_daos_command()
        daos_command.faults_container(
            pool=pool.identifier, cont=container.identifier,
            location="DAOS_CHK_CONT_BAD_LABEL")

        # 3. Try to access property using the original label in CS.
        self.log_step("Try to access property using the original label in CS.")
        try:
            _ = daos_command.container_get_prop(
                pool=pool.identifier, cont=container.identifier)
        except CommandFailure:
            pass
        else:
            self.fail("Label inconsistency fault wasn't injected property!")

        # 4. Show that PS has the original label by using "new-label", which was injected
        # by the fault injection tool.
        cont_prop = daos_command.container_get_prop(
            pool=pool.identifier, cont="new-label", properties=["label"])
        ps_label = cont_prop["response"][0]["value"]
        errors = []
        if ps_label != container.identifier:
            msg = (f"Unexpected label in PS before repair! Expected = "
                   f"{container.identifier}; Actual = {ps_label}")
            errors.append(msg)

        # 5. Enable the checker.
        self.log_step("Enable the checker.")
        dmg_command = self.get_dmg_command()
        dmg_command.check_enable()

        # 6. Set policy to --all-interactive.
        self.log_step("Set policy to --all-interactive.")
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
            if check_query_out["response"]["status"] == "RUNNING" and\
                    check_query_out["response"]["reports"]:
                seq_num = check_query_out["response"]["reports"][0]["seq"]
                break
            time.sleep(5)
        if not seq_num:
            self.fail("Checker didn't detect any fault!")

        # 8. Repair by selecting "Trust the container label in container property."
        self.log_step(
            'Repair by selecting "Trust the container label in container property."')
        dmg_command.check_repair(seq_num=seq_num, action=2)

        # 9. Query the checker until the fault is repaired.
        self.log_step("Query the checker until the fault is repaired.")
        repair_report = self.wait_for_check_complete()[0]

        # Verify that the repair report has expected message "Update the CS label".
        action_message = repair_report["act_msgs"][0]
        exp_msg = "Update the CS label"
        if exp_msg not in action_message:
            errors.append(f"{exp_msg} not in {action_message}!")

        # 10. Disable the checker.
        self.log_step("Disable the checker.")
        dmg_command.check_disable()

        # 11. Verify that the inconsistency was fixed.
        self.log_step("Verify that the inconsistency was fixed.")
        cont_prop = daos_command.container_get_prop(
            pool=pool.identifier, cont=container.identifier, properties=["label"])
        ps_label = cont_prop["response"][0]["value"]
        if ps_label != container.identifier:
            msg = (f"Unexpected label in PS after repair! Expected = "
                   f"{container.identifier}; Actual = {ps_label}")
            errors.append(msg)

        report_errors(test=self, errors=errors)
