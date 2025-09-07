"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from exception_utils import CommandFailure
from general_utils import report_errors
from recovery_utils import wait_for_check_complete


class DMGCheckStopTest(TestWithServers):
    """Test stopping checker during repair.

    :avocado: recursive
    """

    def get_pool_2_phase(self, dmg_command, pool_2):
        """Start checker and get phase entry of pool 2's result.

        Args:
            dmg_command (DmgCommand): Used to call check commands.
            pool_2 (TestPool): Pool 2 object to get phase entry.

        Returns:
            str: Pool 2 phase entry such as CSP_DONE.

        """
        dmg_command.check_start()
        wait_for_check_complete(dmg=dmg_command)
        # In check query output, "status" becomes COMPLETED even only one pool is fixed,
        # so check pool 2.
        check_query_out = dmg_command.check_query()
        repaired_pools = check_query_out["response"]["pools"]
        pool_2_results = repaired_pools[pool_2.uuid.lower()]
        return pool_2_results["phase"]

    def test_stop_during_repair(self):
        """Test stopping checker during repair.

        1. Create two pools 1 and 2. Create a container in 2.
        2. Inject orphan pool in 1 and orphan container in 2.
        3. Enable checker.
        4. Start checker. Immediately after the start command, stop pool 2.
        5. Query and verify that only 1 is fixed.
        6. Start the checker. If 2 isn’t fixed, stop and restart. Verify that 2 is fixed.
        7. Verify that the faults are actually fixed.

        Jira ID: DAOS-17785

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=recovery,cat_recov
        :avocado: tags=DMGCheckStopTest,test_stop_during_repair
        """
        # 1. Create two pools 1 and 2. Create a container in 2.
        self.log_step("Create two pools 1 and 2. Create a container in 2.")
        pool_1 = self.get_pool(connect=False)
        pool_2 = self.get_pool(connect=False)
        container = self.get_container(pool=pool_2)

        # 2. Inject orphan pool in 1 and orphan container in 2.
        self.log_step("Inject orphan pool in 1 and orphan container in 2.")
        dmg_command = self.get_dmg_command()
        dmg_command.faults_mgmt_svc_pool(
            pool=pool_1.identifier, checker_report_class="CIC_POOL_NONEXIST_ON_MS")
        daos_command = self.get_daos_command()
        daos_command.faults_container(
            pool=pool_2.identifier, cont=container.identifier,
            location="DAOS_CHK_CONT_ORPHAN")

        # 3. Enable checker.
        self.log_step("Enable checker.")
        dmg_command.check_enable()

        # 4. Start checker. Immediately after the start command, stop pool 2.
        self.log_step("Start checker. Immediately after the start command, stop pool 2.")
        dmg_command.check_start()
        dmg_command.check_stop(pool=pool_2.identifier)

        # 5. Query and verify that only 1 is fixed.
        self.log_step("Query and verify that only 1 is fixed.")
        wait_for_check_complete(dmg=dmg_command)
        check_query_out = dmg_command.check_query()
        repaired_pools = check_query_out["response"]["pools"]
        pool_1_results = repaired_pools[pool_1.uuid.lower()]
        pool_2_results = repaired_pools[pool_2.uuid.lower()]
        pool_1_phase = pool_1_results["phase"]
        pool_2_phase = pool_2_results["phase"]
        if pool_1_phase != "CSP_DONE":
            self.fail(f"Pool 1 phase isn't CSP_DONE! {pool_1_results}")
        if pool_2_phase == "CSP_DONE":
            self.fail("Pool 2 is fixed before checker stops the repair!")

        # 6. Start the checker. If 2 isn’t fixed, stop and restart. Verify that 2 is fixed.
        self.log_step("Start the checker. If 2 isn’t fixed, stop and restart.")
        pool_2_phase = self.get_pool_2_phase(dmg_command=dmg_command, pool_2=pool_2)
        errors = []
        # If pool 2 isn't fixed, restart checker. Restart is necessary here depending on
        # the timing of the stop above.
        if pool_2_phase != "CSP_DONE":
            dmg_command.check_stop()
            pool_2_phase = self.get_pool_2_phase(dmg_command=dmg_command, pool_2=pool_2)
            if pool_2_phase != "CSP_DONE":
                errors.append("Pool 2 phase isn't done even after checker restart!")

        # 7. Verify that the faults are actually fixed.
        self.log_step("Verify that the faults are actually fixed.")
        dmg_command.check_disable()
        # For pool 1, call dmg pool list and check that it's listed there.
        pool_list_out = dmg_command.pool_list()
        pool_list = pool_list_out["response"]["pools"]
        pool_1_found = False
        for pool in pool_list:
            if pool["label"] == pool_1.label.value:
                pool_1_found = True
                break
        if not pool_1_found:
            errors.append("Pool 1 wasn't in dmg pool list output after repair!")
        # For pool 2, call daos pool list-containers <pool_2.label> and check that the
        # output is empty.
        container_list_out = daos_command.pool_list_containers(pool=pool_2.identifier)
        if container_list_out["response"]:
            errors.append(f"Pool 2 still has container! {container_list_out['response']}")

        container.skip_cleanup()
        report_errors(test=self, errors=errors)

    def test_disable_during_repair(self):
        """Test disabling checker during repair.

        1. Create a pool and inject fault.
        2. Start checker with interactive mode.
        3. Repair the fault. Immediately after the repair command, disable the checker.
        4. Enable the checker.
        5. Query the checker and verify that Current status is DONE or PAUSED.
        6. If status is PAUSED, start the checker. Query and verify that status is
        COMPLETED.
        7. Verify that the fault is actually fixed.

        Jira ID: DAOS-17785

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=recovery,cat_recov
        :avocado: tags=DMGCheckStopTest,test_disable_during_repair
        """
        # 1. Create a pool and inject fault.
        self.log_step("Create a pool and inject fault.")
        pool = self.get_pool(connect=False)
        dmg_command = self.get_dmg_command()
        dmg_command.faults_mgmt_svc_pool(
            pool=pool.identifier, checker_report_class="CIC_POOL_NONEXIST_ON_MS")

        # 2. Start checker with interactive mode.
        self.log_step("Start checker with interactive mode.")
        dmg_command.check_enable()
        dmg_command.check_set_policy(all_interactive=True)
        dmg_command.check_start()

        # 3. Repair the fault. Immediately after the repair command, disable the checker.
        msg = ("Repair the fault. Immediately after the repair command, disable the "
               "checker.")
        self.log_step(msg)
        query_reports = None
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            if check_query_out["response"]["status"] == "RUNNING":
                query_reports = check_query_out["response"]["reports"]
                break
            time.sleep(5)
        seq_num = query_reports[0]["seq"]
        dmg_command.check_repair(seq_num=str(seq_num), action="0")
        dmg_command.check_disable(start=False)

        # 4. Enable the checker.
        self.log_step("Enable the checker.")
        dmg_command.check_enable(stop=False)

        # 5. Query the checker and verify that Current status is DONE or PAUSED.
        self.log_step(
            "Query the checker and verify that Current status is DONE or PAUSED.")
        status_paused = False
        status_done = False
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            status = check_query_out["response"]["status"]
            if status == "DONE":
                status_done = True
                break
            if status == "PAUSED":
                status_paused = True
                break
            time.sleep(5)
        if not status_paused and not status_done:
            self.fail("Check status didn't become DONE or PAUSED after reenable!")

        # 6. If status is PAUSED, start the checker. Query and verify that status is
        # COMPLETED.
        msg = ("If status is PAUSED, start the checker. Query and verify that status is "
               "COMPLETED.")
        self.log_step(msg)
        if status_paused:
            status_completed = False
            dmg_command.check_start()
            for _ in range(8):
                check_query_out = dmg_command.check_query()
                status = check_query_out["response"]["status"]
                if status == "COMPLETED":
                    status_completed = True
                    break
                time.sleep(5)
            if not status_completed:
                self.fail("Checker didn't fix after re-enable!")

        # 7. Verify that the fault is actually fixed.
        self.log_step("Verify that the fault is actually fixed.")
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
