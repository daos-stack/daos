"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from ClusterShell.NodeSet import NodeSet
from general_utils import check_file_exists, report_errors
from recovery_utils import check_ram_used, wait_for_check_complete
from run_utils import command_as_user, run_remote


class PoolListConsolidationTest(TestWithServers):
    """Test Pass 1: Pool List Consolidation

    :avocado: recursive
    """

    def chk_dist_checker(self, inconsistency, policies=None):
        """Run DAOS checker with kinds of options.

        1. Enable check mode.
        2. Run checker under dry-run mode.
        3. Set repair policy as interaction.
        4. Run checker under auto mode and verify that it detects inconsistency.
        5. Reset repair policy as default.
        6. Run checker under regular mode, that will repair the inconsistency.
        7. Disable check mode.

        Jira ID: DAOS-13047

        Args:
            inconsistency (str): The message string for the inconsistency to be detected.
            policies (str, optional): Policies used during dmg check start. Defaults to None.

        Returns:
            list: Errors.

        """
        errors = []

        dmg_command = self.get_dmg_command()
        # 1. Enable check mode.
        self.log_step("Enable check mode")
        dmg_command.check_enable()

        # 2.1 Start checker with "dry-run" option.
        # that will detect the inconsistency but not really repair it.
        self.log_step("Start checker with 'dry-run' option")
        dmg_command.check_start(dry_run=True)

        # 2.2 Query the checker and verify that the checker detected the inconsistency.
        self.log_step("Verify that the checker detected the inconsistency")
        query_msg = wait_for_check_complete(dmg_command)[0]["msg"]
        if inconsistency not in query_msg:
            errors.append(f"Checker didn't detect the {inconsistency} (1)! msg = {query_msg}")
            dmg_command.check_disable()
            return errors

        # 3. Set the repair policy to interaction.
        self.log_step("Set the repair policy to interaction")
        dmg_command.check_set_policy(all_interactive=True)

        # 4.1 start checker with "auto" option,
        # that will detect the inconsistency but skip interaction.
        self.log_step(
            "Start checker with 'auto' option to detect the inconsistency but skip interaction")
        dmg_command.check_start(auto="on")

        # 4.2. Query the checker and verify that the checker detected the inconsistency.
        self.log_step("Verify that the checker detected the inconsistency")
        query_msg = wait_for_check_complete(dmg_command)[0]["msg"]
        if inconsistency not in query_msg:
            errors.append(f"Checker didn't detect the {inconsistency} (2)! msg = {query_msg}")
            dmg_command.check_disable()
            return errors

        # 5. Reset the repair policy to default.
        self.log_step("Reset the repair policy to default")
        dmg_command.check_set_policy(reset_defaults=True)

        # 6.1 Start check with auto=off, that will find the inconsistency and repair it.
        self.log_step("Start check with 'auto=off' to find the inconsistency and repair it")
        dmg_command.check_start(auto="off", policies=policies)

        # 6.2 Query the checker and verify that the checker detected the inconsistency.
        self.log_step("Verify that the checker detected the inconsistency")
        query_msg = wait_for_check_complete(dmg_command)[0]["msg"]
        if inconsistency not in query_msg:
            errors.append(f"Checker didn't detect the {inconsistency} (3)! msg = {query_msg}")

        # 7. Disable check mode.
        self.log_step("Disable check mode")
        dmg_command.check_disable()

        return errors

    def test_dangling_pool(self):
        """Test dangling pool.

        1. Create a pool.
        2. Remove the pool from the pool shards on engine by calling:
        dmg faults pool-svc <pool> CIC_POOL_NONEXIST_ON_ENGINE
        3. Show dangling pool entry by calling:
        dmg pool list --no-query
        4. Run DAOS checker under kinds of mode.
        5. Verify that the dangling pool was removed. Call dmg pool list and it should
        return an empty list.

        Jira ID: DAOS-11711

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_list_consolidation,faults
        :avocado: tags=PoolListConsolidationTest,test_dangling_pool
        """
        # 1. Create a pool.
        pool = self.get_pool(connect=False)

        # 2. Remove the pool shards on engine.
        self.log_step("Remove the pool shards on engine")
        dmg_command = self.get_dmg_command()
        dmg_command.faults_pool_svc(
            pool=pool.identifier, checker_report_class="CIC_POOL_NONEXIST_ON_ENGINE")

        # 3. Show dangling pool entry.
        self.log_step("Show the dangling pool entry")
        pools = dmg_command.get_pool_list_labels(no_query=True)
        if pool.identifier not in pools:
            self.fail("Dangling pool was not found!")

        errors = []
        # 4. Run DAOS checker under kinds of mode.
        errors = self.chk_dist_checker(inconsistency="dangling pool")

        # 5. Verify that the dangling pool was removed.
        self.log_step("Verify that the dangling pool was removed")
        pools = dmg_command.get_pool_list_labels()
        if pools:
            errors.append(f"Dangling pool was not removed! {pools}")

        # Don't try to destroy the pool during tearDown.
        pool.skip_cleanup()

        report_errors(test=self, errors=errors)

    def run_checker_on_orphan_pool(self, pool, policies=None):
        """Run step 1 to 4 of the orphan pool tests.

        1. Remove the PS entry on management service (MS) by calling:
        dmg faults mgmt-svc pool <pool> CIC_POOL_NONEXIST_ON_MS
        2. At this point, MS doesn't recognize any pool, but it exists on engine (orphan
        pool). Call dmg pool list and verify that it doesn't return any pool.
        3. Run DAOS checker under kinds of mode.

        Args:
            pool (TestPool): pool on which to run orphan tests
            policies (str): Policies used during dmg check start. Defaults to None.

        Returns:
            list: Errors.

        """
        # 1. Remove the PS entry on management service (MS).
        dmg_command = self.get_dmg_command()
        dmg_command.faults_mgmt_svc_pool(
            pool=pool.identifier, checker_report_class="CIC_POOL_NONEXIST_ON_MS")

        # 2. At this point, MS doesn't recognize any pool, but it exists on engine.
        pools = dmg_command.get_pool_list_labels()
        if pools:
            msg = f"MS recognized a pool after injecting CIC_POOL_NONEXIST_ON_MS! {pools}"
            self.fail(msg)

        errors = []
        # 3. Run DAOS checker under kinds of mode.
        errors = self.chk_dist_checker(inconsistency="orphan pool", policies=policies)

        return errors

    def verify_pool_dir_removed(self, pool, errors):
        """Verify pool directory was removed from mount point of all nodes.

        Args:
            errors (list): Error list.

        Returns:
            list: Error list.

        """
        pool_path = self.server_managers[0].get_vos_path(pool)
        check_out = check_file_exists(
            hosts=self.hostlist_servers, filename=pool_path, directory=True)
        if check_out[0]:
            msg = f"Pool path still exists! Node without pool path = {check_out[1]}"
            errors.append(msg)

        return errors

    def test_orphan_pool_trust_ps(self):
        """Test orphan pool with trust PS (default option).

        Jira ID: DAOS-11712

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_list_consolidation,faults
        :avocado: tags=PoolListConsolidationTest,test_orphan_pool_trust_ps
        """
        # 1. Create a pool.
        pool = self.get_pool(connect=False)

        errors = []
        # 2. Run DAOS checker under kinds of mode with trusting PS (by default).
        self.log_step("Run DAOS checker under kinds of mode with trusting PS (by default)")
        errors = self.run_checker_on_orphan_pool(pool)

        # 3. Verify that the orphan pool was reconstructed.
        self.log_step("Verify that the orphan pool was reconstructed")
        dmg_command = self.get_dmg_command()
        pools = dmg_command.get_pool_list_labels()
        if pool.identifier not in pools:
            errors.append(f"Orphan pool was not reconstructed! Pools = {pools}")

        report_errors(test=self, errors=errors)

    def test_orphan_pool_trust_ms(self):
        """Test orphan pool with trust MS.

        Jira ID: DAOS-11712

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_list_consolidation,faults
        :avocado: tags=PoolListConsolidationTest,test_orphan_pool_trust_ms
        """
        # 1. Create a pool.
        pool = self.get_pool(connect=False)

        errors = []
        # 2. Run DAOS checker under kinds of mode with trusting MS.
        self.log_step("Run DAOS checker under kinds of mode with trusting MS")
        errors = self.run_checker_on_orphan_pool(pool, policies="POOL_NONEXIST_ON_MS:CIA_TRUST_MS")

        # 3. Verify that the orphan pool was destroyed.
        self.log_step("Verify that the orphan pool was destroyed")
        dmg_command = self.get_dmg_command()
        pools = dmg_command.get_pool_list_labels()
        if pools:
            errors.append(f"Orphan pool was not destroyed! Pools = {pools}")

        # 4. Verify that the pool directory is removed from the mount point.
        self.log_step("Verify that the pool directory is removed from the mount point")
        errors = self.verify_pool_dir_removed(pool, errors=errors)

        # Don't try to destroy the pool during tearDown.
        pool.skip_cleanup()

        report_errors(test=self, errors=errors)

    def test_lost_majority_ps_replicas(self):
        """Test lost the majority of PS replicas.

        1. Create a pool with --nsvc=3.
        2. Determine the host with two service ranks.
        3. Stop servers.
        4. Remove rdb-pool from the host determined in step 2. Remove from both mount
        points.
        5. Run DAOS checker under kinds of mode.
        6. Try creating a container. The pool can be started now, so create should succeed.
        7. Show that rdb-pool are recovered. i.e., at least three out of four ranks
        should have rdb-pool.

        Jira ID: DAOS-12029

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_list_consolidation
        :avocado: tags=PoolListConsolidationTest,test_lost_majority_ps_replicas
        """
        # If the test runs on MD-on-SSD cluster, the "class" field under "storage" would
        # be "ram". If so, skip (pass) the test. (If the test runs on a normal HW Medium
        # cluster, the "class" would be "dcpm".)
        # This test is skipped on MD-on-SSD because if the system is stopped, the data
        # under mount point will be removed, but the test expects the pool dir to exist
        # there.
        ram_used = check_ram_used(server_manager=self.server_managers[0], log=self.log)
        if ram_used:
            self.log.info("MD-on-SSD cluster isn't currently supported.")
            # return results in PASS.
            return

        self.log_step("Create a pool with --nsvc=3.")
        pool = self.get_pool(connect=False, svcn=3)

        vos_path = self.server_managers[0].get_vos_path(pool)
        self.log.debug(f"## vos_path = {vos_path}")

        self.log.debug("Determine the host with two service ranks.")
        # Each rank corresponds to a mount point of a host. However, the mapping could
        # change across server restarts. For example, following configuration could be
        # possible.
        # hsw-100:/mnt/daos0/<pool_uuid> -> Rank 0
        # hsw-100:/mnt/daos1/<pool_uuid> -> Rank 2
        # hsw-101:/mnt/daos0/<pool_uuid> -> Rank 1
        # hsw-101:/mnt/daos1/<pool_uuid> -> Rank 3
        # The goal is to determine the host_name:mount_point from a given service replica
        # rank. e.g., If service replicas are [0, 1, 2], we need to remove rdb-pool from
        # two out of the top three rows above. We can easily get the rank to host mapping
        # (self.server_managers[0].get_host(rank)). However, there's no easy way to
        # determine which mount point the rank is mapped to (daos0 or daos1). Thus, we'll
        # use the following algorithm assuming that this test uses two dual-engine hosts.
        # 1. Determine the non-service replica rank.
        all_rank_set = set(range(0, 4))
        svc_rank_set = set(pool.svc_ranks)
        non_svc_rank_set = all_rank_set - svc_rank_set
        self.log.debug(f"## non_svc_rank_set = {non_svc_rank_set}")
        non_svc_rank = non_svc_rank_set.pop()
        self.log.debug(f"## non_svc_rank = {non_svc_rank}")
        # 2. Get the host of the non_svc_rank. We will not remove rdb-pool from it, so
        # call it non_remove_host.
        rank_to_host = self.server_managers[0].ranks
        non_remove_host = rank_to_host[non_svc_rank]
        # 3. Get the other host of non_remove_host.
        remove_host = None
        for rank in range(0, 4):
            host = rank_to_host[rank]
            if host != non_remove_host:
                remove_host = host
                break
        self.log.debug(f"## remove_host = {remove_host}")

        self.log_step("Stop servers")
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        msg = ("Remove rdb-pool from the host determined in step 2. Remove from both "
                "mount points.")
        self.log_step(msg)
        # vos_path = self.server_managers[0].get_vos_path(pool)
        # self.log.debug(f"## vos_path = {vos_path}")
        # self.log_step("Remove <scm_mount>/<pool_uuid>/rdb-pool from two ranks.")
        self.log_step("Remove /mnt/daos[0,1]/<pool_uuid>/rdb-pool from two ranks.")
        # rdb_pool_path = f"{self.server_managers[0].get_vos_path(pool)}/rdb-pool"
        rdb_pool_path_0 = f"/mnt/daos0/{pool.uuid.lower()}/rdb-pool"
        rdb_pool_path_1 = f"/mnt/daos1/{pool.uuid.lower()}/rdb-pool"
        command = f"sudo rm {rdb_pool_path_0}; sudo rm {rdb_pool_path_1}"
        remove_node = NodeSet(remove_host)
        check_out_0 = check_file_exists(
            hosts=remove_node, filename=rdb_pool_path_0, sudo=True)
        check_out_1 = check_file_exists(
            hosts=remove_node, filename=rdb_pool_path_1, sudo=True)
        if check_out_0[0] and check_out_1[0]:
            if not run_remote(log=self.log, hosts=remove_node, command=command).passed:
                msg = (f"Failed to remove {rdb_pool_path_0} and {rdb_pool_path_1} on "
                       f"{remove_node}")
                self.fail(msg)
            msg = f"{rdb_pool_path_0} and {rdb_pool_path_1} removed from {remove_node}"
            self.log.info(msg)

        self.log_step("Run DAOS checker under kinds of mode.")
        errors = []
        errors = self.chk_dist_checker(
            inconsistency="corrupted pool without quorum")

        self.log_step("Try creating a container. It should succeed.")
        cont_create_success = False
        for _ in range(5):
            time.sleep(5)
            try:
                self.get_container(pool)
                cont_create_success = True
                break
            except TestFail as error:
                msg = f"## Container create failed after running checker! error = {error}"
                self.log.debug(msg)

        if not cont_create_success:
            errors.append("Container create failed after running checker!")

        msg = ("Show that rdb-pool are recovered. i.e., at least three out of four ranks should "
               "have rdb-pool.")
        self.log_step(msg)
        hosts = list(set(self.server_managers[0].ranks.values()))
        count = 0
        rdb_pool_path_list = [rdb_pool_path_0, rdb_pool_path_1]
        for host in hosts:
            node = NodeSet(host)
            for rdb_pool_path in rdb_pool_path_list:
                check_out = check_file_exists(
                    hosts=node, filename=rdb_pool_path, sudo=True)
                if check_out[0]:
                    count += 1
                    self.log.info("## %s found at %s", rdb_pool_path, str(node))

        self.log.info("## rdb-pool count = %d", count)
        if count < len(hosts) - 1:
            errors.append(f"Not enough rdb-pool has been recovered! - {count} ranks")

        report_errors(test=self, errors=errors)

    def test_lost_all_rdb(self):
        """Remove rdb-pool from all mount point from all nodes. Now the pool cannot be
        recovered, so checker should remove it from both MS and engine.

        1. Create a pool.
        2. Stop servers.
        3. Remove <scm_mount>/<pool_uuid>/rdb-pool from all ranks.
        4. Run DAOS checker under kinds of mode.
        5. Check that the pool does not appear with dmg pool list.
        6. Verify that the pool directory was removed from the mount point.

        Jira ID: DAOS-12067

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_list_consolidation
        :avocado: tags=PoolListConsolidationTest,test_lost_all_rdb
        """
        # If the test runs on MD-on-SSD cluster, the "class" field under "storage" would
        # be "ram". If so, skip (pass) the test. (If the test runs on a normal HW Medium
        # cluster, the "class" would be "dcpm".)
        # This test is skipped on MD-on-SSD because if the system is stopped, the data
        # under mount point will be removed, but the test expects the pool dir to exist
        # there.
        ram_used = check_ram_used(server_manager=self.server_managers[0], log=self.log)
        if ram_used:
            self.log.info("MD-on-SSD cluster isn't currently supported.")
            # return results in PASS.
            return

        self.log_step("Create a pool.")
        pool = self.get_pool(connect=False)

        self.log_step("Stop servers.")
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        self.log_step("Remove <scm_mount>/<pool_uuid>/rdb-pool from all ranks.")
        scm_mounts = self.server_managers[0].manager.job.yaml.get_engine_values(
            "scm_mount")
        rdb_pool_paths = [f"{scm}/{pool.uuid.lower()}/rdb-pool" for scm in scm_mounts]
        self.log.info("rdb_pool_paths: %s", rdb_pool_paths)
        for rdb_pool_path in rdb_pool_paths:
            command = command_as_user(command=f"rm {rdb_pool_path}", user="root")
            remove_result = run_remote(
                log=self.log, hosts=self.hostlist_servers, command=command)
            if not remove_result.passed:
                self.fail(
                    f"Failed to remove {rdb_pool_path} from {remove_result.failed_hosts}")

        self.log_step("Run DAOS checker under kinds of mode.")
        errors = []
        errors = self.chk_dist_checker(
            inconsistency="corrupted pool without quorum")

        self.log_step("Check that the pool does not appear with dmg pool list.")
        pools = dmg_command.get_pool_list_all()
        if pools:
            errors.append(f"Pool still exists after running checker! {pools}")

        self.log_step("Verify that the pool directory was removed from the mount point.")
        errors = self.verify_pool_dir_removed(pool, errors=errors)

        # Don't try to destroy the pool during tearDown.
        pool.skip_cleanup()

        report_errors(test=self, errors=errors)
