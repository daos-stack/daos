"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from avocado.core.exceptions import TestFail
from ClusterShell.NodeSet import NodeSet
from general_utils import check_file_exists, pcmd, report_errors
from recovery_test_base import RecoveryTestBase


class PoolListConsolidationTest(RecoveryTestBase):
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
        dmg_command.check_enable()

        # 2.1 Start checker with "dry-run" option.
        # that will detect the inconsistency but not really repair it.
        dmg_command.check_start(dry_run=True)

        # 2.2 Query the checker.
        query_msg = self.wait_for_check_complete()[0]["msg"]

        # 2.3 Verify that the checker detected the inconsistency.
        if inconsistency not in query_msg:
            errors.append(
                "Checker didn't detect the {} (1)! msg = {}".format(inconsistency, query_msg))
            dmg_command.check_disable()
            return errors

        # 3. Set the repair policy to interaction.
        dmg_command.check_set_policy(all_interactive=True)

        # 4.1 start checker with "auto" option,
        # that will detect the inconsistency but skip interaction.
        dmg_command.check_start(auto="on")

        # 4.2. Query the checker.
        query_msg = self.wait_for_check_complete()[0]["msg"]

        # 4.3 Verify that the checker detected the inconsistency.
        if inconsistency not in query_msg:
            errors.append(
                "Checker didn't detect the {} (2)! msg = {}".format(inconsistency, query_msg))
            dmg_command.check_disable()
            return errors

        # 5. Reset the repair policy to default.
        dmg_command.check_set_policy(reset_defaults=True)

        # 6.1 Start check with auto=off,
        # that will find the inconsistency and repair it.
        dmg_command.check_start(auto="off", policies=policies)

        # 6.2 Query the checker.
        query_msg = self.wait_for_check_complete()[0]["msg"]

        # 6.3 Verify that the checker detected the inconsistency.
        if inconsistency not in query_msg:
            errors.append(
                "Checker didn't detect the {} (3)! msg = {}".format(inconsistency, query_msg))

        # 7. Disable check mode.
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
        :avocado: tags=recovery,cat_recov,pool_list_consolidation
        :avocado: tags=PoolListConsolidationTest,test_dangling_pool
        """
        # 1. Create a pool.
        self.pool = self.get_pool(connect=False)

        # 2. Remove the pool shards on engine.
        dmg_command = self.get_dmg_command()
        dmg_command.faults_pool_svc(
            pool=self.pool.identifier, checker_report_class="CIC_POOL_NONEXIST_ON_ENGINE")

        # 3. Show dangling pool entry.
        pools = dmg_command.get_pool_list_labels(no_query=True)
        if self.pool.identifier not in pools:
            self.fail("Dangling pool was not found!")

        errors = []
        # 4. Run DAOS checker under kinds of mode.
        errors = self.chk_dist_checker(inconsistency="dangling pool")

        # 5. Verify that the dangling pool was removed.
        pools = dmg_command.get_pool_list_labels()
        if pools:
            errors.append(f"Dangling pool was not removed! {pools}")

        # Don't try to destroy the pool during tearDown.
        self.pool.skip_cleanup()

        report_errors(test=self, errors=errors)

    def run_checker_on_orphan_pool(self, policies=None):
        """Run step 1 to 4 of the orphan pool tests.

        1. Create a pool.
        2. Remove the PS entry on management service (MS) by calling:
        dmg faults mgmt-svc pool <pool> CIC_POOL_NONEXIST_ON_MS
        3. At this point, MS doesn't recognize any pool, but it exists on engine (orphan
        pool). Call dmg pool list and verify that it doesn't return any pool.
        4. Run DAOS checker under kinds of mode.

        Args:
            policies (str): Policies used during dmg check start. Defaults to None.

        Returns:
            list: Errors.

        """
        # 1. Create a pool.
        self.pool = self.get_pool(connect=False)

        # 2. Remove the PS entry on management service (MS).
        dmg_command = self.get_dmg_command()
        dmg_command.faults_mgmt_svc_pool(
            pool=self.pool.identifier, checker_report_class="CIC_POOL_NONEXIST_ON_MS")

        # 3. At this point, MS doesn't recognize any pool, but it exists on engine.
        pools = dmg_command.get_pool_list_labels()
        if pools:
            msg = f"MS recognized a pool after injecting CIC_POOL_NONEXIST_ON_MS! {pools}"
            self.fail(msg)

        errors = []
        # 4. Run DAOS checker under kinds of mode.
        errors = self.chk_dist_checker(
            inconsistency="orphan pool", policies=policies)

        return errors

    def verify_pool_dir_removed(self, errors):
        """Verify pool directory was removed from mount point of all nodes.

        Args:
            errors (list): Error list.

        Returns:
            list: Error list.

        """
        hosts = list(set(self.server_managers[0].ranks.values()))
        nodeset_hosts = NodeSet.fromlist(hosts)
        pool_path = f"/mnt/daos0/{self.pool.uuid.lower()}"
        check_out = check_file_exists(hosts=nodeset_hosts, filename=pool_path)
        if check_out[0]:
            msg = f"Pool path still exists! Node without pool path = {check_out[1]}"
            errors.append(msg)

        return errors

    def test_orphan_pool_trust_ps(self):
        """Test orphan pool with trust PS (default option).

        Jira ID: DAOS-11712

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_list_consolidation
        :avocado: tags=PoolListConsolidationTest,test_orphan_pool_trust_ps
        """
        errors = []
        # 1. Run DAOS checker under kinds of mode with trusting PS (by default).
        errors = self.run_checker_on_orphan_pool()

        # 2. Verify that the orphan pool was reconstructed.
        dmg_command = self.get_dmg_command()
        pools = dmg_command.get_pool_list_labels()
        if self.pool.identifier not in pools:
            errors.append(f"Orphan pool was not reconstructed! Pools = {pools}")

        report_errors(test=self, errors=errors)

    def test_orphan_pool_trust_ms(self):
        """Test orphan pool with trust MS.

        Jira ID: DAOS-11712

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_list_consolidation
        :avocado: tags=PoolListConsolidationTest,test_orphan_pool_trust_ms
        """
        errors = []
        # 1. Run DAOS checker under kinds of mode with trusting MS.
        errors = self.run_checker_on_orphan_pool(
            policies="POOL_NONEXIST_ON_MS:CIA_TRUST_MS")

        # 2. Verify that the orphan pool was destroyed.
        dmg_command = self.get_dmg_command()
        pools = dmg_command.get_pool_list_labels()
        if pools:
            errors.append(f"Orphan pool was not destroyed! Pools = {pools}")

        # 3. Verify that the pool directory is removed from the mount point.
        errors = self.verify_pool_dir_removed(errors=errors)

        # Don't try to destroy the pool during tearDown.
        self.pool.skip_cleanup()

        report_errors(test=self, errors=errors)

    def test_lost_majority_ps_replicas(self):
        """Test lost the majority of PS replicas.

        1. Create a pool with --nsvc=3. Rank 0, 1, and 2 will be pool service replicas.
        2. Stop servers.
        3. Remove <scm_mount>/<pool_uuid>/rdb-pool from rank 0 and 2.
        4. Start servers.
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
        self.log_step("Create a pool with --nsvc=3.")
        self.pool = self.get_pool(svcn=3)

        self.log_step("Stop servers")
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        self.log_step("Remove <scm_mount>/<pool_uuid>/rdb-pool from two ranks.")
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        scm_mount_result = check_file_exists(
            hosts=self.hostlist_servers, filename=scm_mount, directory=True)
        if not scm_mount_result[0]:
            msg = "MD-on-SSD cluster. Mount point is removed by control plane after system stop."
            self.log.info(msg)
            dmg_command.system_start()
            # return results in PASS.
            return
        rdb_pool_path = f"{scm_mount}/{self.pool.uuid.lower()}/rdb-pool"
        command = f"sudo rm {scm_mount}/{self.pool.uuid.lower()}/rdb-pool"
        hosts = list(set(self.server_managers[0].ranks.values()))
        count = 0
        for host in hosts:
            node = NodeSet(host)
            check_out = check_file_exists(hosts=node, filename=rdb_pool_path, sudo=True)
            if check_out[0]:
                pcmd(hosts=node, command=command)
                self.log.info("rm rdb-pool from %s", str(node))
                count += 1
                if count > 1:
                    break

        self.log_step("Start servers.")
        dmg_command.system_start()

        self.log_step("Run DAOS checker under kinds of mode.")
        errors = []
        errors = self.chk_dist_checker(
            inconsistency="corrupted pool without quorum")

        self.log_step("Try creating a container. It should succeed.")
        cont_create_success = False
        for _ in range(5):
            time.sleep(5)
            try:
                self.container = self.get_container(pool=self.pool)
                cont_create_success = True
                break
            except TestFail as error:
                msg = (f"## Container create failed after running checker! "
                       f"error = {error}")
                self.log.debug(msg)

        if not cont_create_success:
            errors.append("Container create failed after running checker!")

        msg = ("Show that rdb-pool are recovered. i.e., at least three out of four ranks should "
               "have rdb-pool.")
        self.log_step(msg)
        hosts = list(set(self.server_managers[0].ranks.values()))
        count = 0
        for host in hosts:
            node = NodeSet(host)
            check_out = check_file_exists(hosts=node, filename=rdb_pool_path, sudo=True)
            if check_out[0]:
                count += 1
                self.log.info("rdb-pool found at %s", str(node))

        self.log.info("rdb-pool count = %d", count)
        if count < len(hosts) - 1:
            errors.append(f"Not enough rdb-pool has been recovered! - {count} ranks")

        report_errors(test=self, errors=errors)

    def test_lost_all_rdb(self):
        """Remove rdb-pool from all mount point from all nodes. Now the pool cannot be
        recovered, so checker should remove it from both MS and engine.

        1. Create a pool.
        2. Stop servers.
        3. Remove <scm_mount>/<pool_uuid>/rdb-pool from all ranks.
        4. Start servers.
        5. Run DAOS checker under kinds of mode.
        6. Check that the pool does not appear with dmg pool list.
        7. Verify that the pool directory was removed from the mount point.

        Jira ID: DAOS-12067

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_list_consolidation
        :avocado: tags=PoolListConsolidationTest,test_lost_all_rdb
        """
        self.log_step("Create a pool.")
        self.pool = self.get_pool()

        self.log_step("Stop servers.")
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        self.log_step("Remove <scm_mount>/<pool_uuid>/rdb-pool from all ranks.")
        hosts = list(set(self.server_managers[0].ranks.values()))
        nodeset_hosts = NodeSet.fromlist(hosts)
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        scm_mount_result = check_file_exists(
            hosts=self.hostlist_servers, filename=scm_mount, directory=True)
        if not scm_mount_result[0]:
            msg = "MD-on-SSD cluster. Mount point is removed by control plane after system stop."
            self.log.info(msg)
            dmg_command.system_start()
            # return results in PASS.
            return
        command = f"sudo rm {scm_mount}/{self.pool.uuid.lower()}/rdb-pool"
        remove_result = pcmd(hosts=nodeset_hosts, command=command)
        success_nodes = remove_result[0]
        if nodeset_hosts != success_nodes:
            msg = (f"Failed to remove rdb-pool! All = {nodeset_hosts}, "
                   f"Success = {success_nodes}")
            self.fail(msg)

        # 4. Start servers.
        self.log_step("Start servers.")
        dmg_command.system_start()

        self.log_step("Run DAOS checker under kinds of mode.")
        errors = []
        errors = self.chk_dist_checker(
            inconsistency="corrupted pool without quorum")

        self.log_step("Check that the pool does not appear with dmg pool list.")
        pools = dmg_command.get_pool_list_all()
        if pools:
            errors.append(f"Pool still exists after running checker! {pools}")

        self.log_step("Verify that the pool directory was removed from the mount point.")
        errors = self.verify_pool_dir_removed(errors=errors)

        # Don't try to destroy the pool during tearDown.
        self.pool.skip_cleanup()

        report_errors(test=self, errors=errors)
