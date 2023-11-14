"""
  (C) Copyright 2023 Intel Corporation.

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
        5. Use ddb to verify that the container is left in shards.
        6. Enable the checker.
        7. Set policy to --all-interactive.
        8. Start the checker and query the checker until the fault is detected.
        9. Repair by selecting the destroy option.
        10. Query the checker until the fault is repaired.
        11. Disable the checker.
        12. Run the ddb command and verify that the container is removed from shard.

        Jira ID: DAOS-12287

        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=recovery,container_list_consolidation
        :avocado: tags=ContainerListConsolidationTest,test_orphan_container
        """
        # 1. Create a pool and a container.
        self.log_step("Create a pool and a container")
        pool = self.get_pool(connect=False)
        container = self.get_container(pool=pool)

        # 2. Inject fault to cause orphan container.
        self.log_step("Inject fault to cause orphan container.")
        daos_command = self.get_daos_command()
        daos_command.faults_container(
            pool=pool.identifier, cont=container.identifier,
            location="DAOS_CHK_CONT_ORPHAN")

        # 3. Check that the container doesn't appear with daos command.
        self.log_step("Check that the container doesn't appear with daos command.")
        pool_list = daos_command.pool_list_containers(pool=pool.identifier)
        errors = []
        if pool_list["response"]:
            errors.append(f"Container appears with daos command! {pool_list}")

        # 4. Stop servers.
        self.log_step("Stop servers.")
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 5. Use ddb to verify that the container is left in shards.
        self.log_step("Use ddb to verify that the container is left in shards.")
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        ddb_command = DdbCommand(
            server_host=NodeSet(self.hostlist_servers[0]), path=self.bin,
            mount_point=scm_mount, pool_uuid=pool.uuid,
            vos_file=self.get_vos_file_path(pool=pool))
        cmd_result = ddb_command.list_component()
        ls_out = "\n".join(cmd_result[0]["stdout"])
        uuid_regex = r"([0-f]{8}-[0-f]{4}-[0-f]{4}-[0-f]{4}-[0-f]{12})"
        match = re.search(uuid_regex, ls_out)
        if match is None:
            self.fail("Unexpected output from ddb command, unable to parse.")
        self.log.info("Container UUID from ddb ls = %s", match.group(1))

        # UUID if found. Verify that it's the container UUID of the container we created.
        actual_uuid = match.group(1)
        expected_uuid = container.uuid.lower()
        if actual_uuid != expected_uuid:
            msg = "Unexpected container UUID! Expected = {}; Actual = {}".format(
                expected_uuid, actual_uuid)
            errors.append(msg)

        # 6. Enable the checker.
        self.log_step("Enable the checker.")
        dmg_command.check_enable(stop=False)

        # 7. Set policy to --all-interactive.
        self.log_step("Set policy to --all-interactive.")
        dmg_command.check_set_policy(all_interactive=True)

        # 8. Start the checker and query the checker until the fault is detected.
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

        # 9. Repair by selecting the destroy option, 0.
        msg = ("Repair with option 0; Destroy the orphan container to release space "
               "[suggested].")
        self.log_step(msg)
        dmg_command.check_repair(seq_num=seq_num, action=0)

        # 10. Query the checker until the fault is repaired.
        self.log_step("Query the checker until the fault is repaired.")
        repair_report = self.wait_for_check_complete()[0]

        # Verify that the repair report has expected message "Discard the container".
        action_message = repair_report["act_msgs"][0]
        exp_msg = "Discard the container"
        errors = []
        if exp_msg not in action_message:
            errors.append(f"{exp_msg} not in {action_message}!")

        # 11. Disable the checker.
        self.log_step("Disable the checker.")
        dmg_command.check_disable(start=False)

        # 12. Run the ddb command and verify that the container is removed from shard.
        self.log_step(
            "Run the ddb command and verify that the container is removed from shard.")
        cmd_result = ddb_command.list_component()
        ls_out = "\n".join(cmd_result[0]["stdout"])
        uuid_regex = r"([0-f]{8}-[0-f]{4}-[0-f]{4}-[0-f]{4}-[0-f]{12})"
        match = re.search(uuid_regex, ls_out)
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
