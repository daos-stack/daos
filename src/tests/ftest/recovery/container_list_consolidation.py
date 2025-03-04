"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
import time

from ClusterShell.NodeSet import NodeSet
from ddb_utils import DdbCommand
from exception_utils import CommandFailure
from general_utils import report_errors
from recovery_test_base import RecoveryTestBase


class ContainerListConsolidationTest(RecoveryTestBase):
    """Test Pass 4: Container List Consolidation

    :avocado: recursive
    """

    def test_orphan_container(self):
        """Test orphan container. Container is in shard, but not in PS.

        1. Create a pool and a container.
        2. Inject fault to cause orphan container. i.e., container is left in the system,
        but doesn't appear with daos commands.
        3. Check that the container doesn't appear with daos command.
        4. Stop servers.
        5. Use ddb to verify that the container is left in shards (PMEM only).
        6. Enable the checker.
        7. Set policy to --all-interactive.
        8. Start the checker and query the checker until the fault is detected.
        9. Repair by selecting the destroy option.
        10. Query the checker until the fault is repaired.
        11. Disable the checker.
        12. Run the ddb command and verify that the container is removed from shard (PMEM only).

        Jira ID: DAOS-12287

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,container_list_consolidation,faults
        :avocado: tags=ContainerListConsolidationTest,test_orphan_container
        """
        self.log_step("Create a pool and a container")
        pool = self.get_pool(connect=False)
        container = self.get_container(pool=pool)
        expected_uuid = container.uuid.lower()

        self.log_step("Inject fault to cause orphan container.")
        daos_command = self.get_daos_command()
        daos_command.faults_container(
            pool=pool.identifier, cont=container.identifier,
            location="DAOS_CHK_CONT_ORPHAN")
        container.skip_cleanup()

        self.log_step("Check that the container doesn't appear with daos command.")
        pool_list = daos_command.pool_list_containers(pool=pool.identifier)
        errors = []
        if pool_list["response"]:
            errors.append(f"Container appears with daos command! {pool_list}")

        self.log_step("Stop servers.")
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        self.log_step("Use ddb to verify that the container is left in shards (PMEM only).")
        vos_file = self.get_vos_file_path(pool=pool)
        if vos_file:
            # We're using a PMEM cluster.
            scm_mount = self.server_managers[0].get_config_value("scm_mount")
            ddb_command = DdbCommand(
                server_host=NodeSet(self.hostlist_servers[0]), path=self.bin,
                mount_point=scm_mount, pool_uuid=pool.uuid, vos_file=vos_file)
            cmd_result = ddb_command.list_component()
            uuid_regex = r"([0-f]{8}-[0-f]{4}-[0-f]{4}-[0-f]{4}-[0-f]{12})"
            match = re.search(uuid_regex, cmd_result.joined_stdout)
            if match is None:
                self.fail("Unexpected output from ddb command, unable to parse.")
            self.log.info("Container UUID from ddb ls = %s", match.group(1))

            # UUID is found. Verify that it's the container UUID of the container we created.
            actual_uuid = match.group(1)
            if actual_uuid != expected_uuid:
                msg = "Unexpected container UUID! Expected = {}; Actual = {}".format(
                    expected_uuid, actual_uuid)
                errors.append(msg)

        self.log_step("Enable the checker.")
        dmg_command.check_enable(stop=False)

        self.log_step("Set policy to --all-interactive.")
        dmg_command.check_set_policy(all_interactive=True)

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

        msg = ("Repair with option 0; Destroy the orphan container to release space "
               "[suggested].")
        self.log_step(msg)
        dmg_command.check_repair(seq_num=seq_num, action=0)

        self.log_step("Query the checker until the fault is repaired.")
        repair_report = self.wait_for_check_complete()[0]

        action_message = repair_report["act_msgs"][0]
        exp_msg = "Discard the container"
        errors = []
        if exp_msg not in action_message:
            errors.append(f"{exp_msg} not in {action_message}!")

        self.log_step("Disable the checker.")
        dmg_command.check_disable(start=False)

        if vos_file:
            # ddb requires the vos file. PMEM cluster only.
            msg = ("Run the ddb command and verify that the container is removed from shard "
                   "(PMEM only).")
            self.log_step(msg)
            cmd_result = ddb_command.list_component()
            uuid_regex = r"([0-f]{8}-[0-f]{4}-[0-f]{4}-[0-f]{4}-[0-f]{12})"
            match = re.search(uuid_regex, cmd_result.joined_stdout)
            if match:
                errors.append("Container UUID is found in shard! Checker didn't remove it.")

        # Start server to prepare for the cleanup.
        try:
            dmg_command.system_start()
        except CommandFailure as error:
            # Handle the potential system start error just in case.
            self.log.error(error)
        finally:
            report_errors(test=self, errors=errors)

        # Remove container object so that tearDown will not try to destroy the non-existent
        # container.
        container.skip_cleanup()
