"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from general_utils import check_file_exists
from recovery_test_base import RecoveryTestBase
from run_utils import run_remote


class DMGCheckStartOptionsTest(RecoveryTestBase):
    """Test dmg check start options.

    Options list. (Not all options are tested here.)
    -n, --dry-run           Scan only; do not initiate repairs.
    -r, --reset             Reset the system check state.
    -f, --failout=[on|off]  Stop on failure.
    -a, --auto=[on|off]     Attempt to automatically repair problems.
    -O, --find-orphans      Find orphaned pools.
    -p, --policies=         Set repair policies.

    :avocado: recursive
    """

    def test_check_start_reset(self):
        """Test dmg check start --reset.

        See the state diagram attached to the ticket.

        1. Create a pool.
        2. Inject orphan pool fault.
        3. Start the checker with interactive mode. This will move the state to "checking"
        from "unchecked".
        4. Verify that the orphan pool is detected.
        5. Stop the checker. The state is now at "stopped".
        6. Remove the pool directory from the mount point.
        7. Start the checker without --reset. State is back to "checking".
        8. Verify that the action entry is still there.
        9. Stop the checker. State is "stopped".
        10. Start the checker with --reset. The state should have transitioned to
        "unchecked", then "checking".
        11. Verify that the action entry is empty and the status is COMPLETED.

        Jira ID: DAOS-17623

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=recovery,cat_recov
        :avocado: tags=DMGCheckStartOptionsTest,test_check_start_reset
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
        repair_reports = None
        for _ in range(8):
            check_query_out = self.get_dmg_command().check_query()
            if check_query_out["response"]["status"] == "RUNNING":
                repair_reports = check_query_out["response"]["reports"]
                break
            time.sleep(5)
        if not repair_reports:
            self.fail("Checker didn't detect any inconsistency!")
        fault_msg = repair_reports[0]["msg"]
        orphan_pool = "orphan pool"
        if orphan_pool not in fault_msg:
            msg = (f"Checker didn't detect the {orphan_pool} (1)! Fault msg = "
                   f"{fault_msg}")
            self.fail(msg)

        # 5. Stop the checker.
        self.log_step("Stop the checker.")
        dmg_command.check_stop()

        # 6. Remove the pool directory from the mount point.
        self.log_step("Remove the pool directory from the mount point.")
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        pool_path = f"{scm_mount}/{pool.uuid.lower()}"
        pool_out = check_file_exists(
            hosts=self.hostlist_servers, filename=pool_path, sudo=True)
        if not pool_out[0]:
            msg = ("MD-on-SSD cluster. Contents under mount point are removed by control "
                   "plane after system stop.")
            self.log.info(msg)
            dmg_command.system_start()
            # return results in PASS.
            return
        command = f"sudo rm -rf {pool_path}"
        remove_result = run_remote(
            log=self.log, hosts=self.hostlist_servers, command=command)
        if not remove_result.passed:
            self.fail(f"Failed to remove {pool_path} from {remove_result.failed_hosts}")

        # 7. Start the checker without --reset.
        self.log_step("Start the checker without --reset.")
        dmg_command.check_start()

        # 8. Verify that the action entry is still there.
        self.log_step("Verify that the action entry is still there.")
        # At this point, the status is STOPPED (it will not turn to RUNNING), so just
        # check whether msg contains "orphan pool".
        check_query_out = self.get_dmg_command().check_query()
        if not repair_reports:
            self.fail("Checker didn't detect any inconsistency!")
        fault_msg = repair_reports[0]["msg"]
        if orphan_pool not in fault_msg:
            msg = (f"Checker didn't detect the {orphan_pool} (2)! Fault msg = "
                   f"{fault_msg}")
            dmg_command.check_disable()
            self.fail(msg)

        # 9. Stop the checker.
        self.log_step("Stop the checker.")
        dmg_command.check_stop()

        # 10. Start the checker with --reset.
        self.log_step("Start the checker with --reset.")
        dmg_command.check_start(reset=True)

        # 11. Verify that the action entry is empty and the status is COMPLETED.
        self.log_step(
            "Verify that the action entry is empty and the status is COMPLETED.")
        repair_reports = None
        check_completed = False
        for _ in range(8):
            check_query_out = self.get_dmg_command().check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                # "reports" field is expected to be None.
                repair_reports = check_query_out["response"]["reports"]
                check_completed = True
                break
            time.sleep(5)
        if not check_completed:
            self.fail("Status is not COMPLETED!")
        if repair_reports:
            msg = f"Status is COMPLETED, but repair reports isn't empty! {repair_reports}"
            self.fail(msg)

        # Disable the checker to prepare for the tearDown.
        dmg_command.check_disable()
        # The pool is orphan pool, so skip the cleanup.
        pool.skip_cleanup()
