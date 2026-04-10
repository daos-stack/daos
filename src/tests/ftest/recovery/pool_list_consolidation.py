"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from ClusterShell.NodeSet import NodeSet
from ddb_utils import DdbCommand
from general_utils import check_file_exists, report_errors
from recovery_utils import wait_for_check_complete
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
        pool_path = self.server_managers[0].get_vos_paths(pool)[0]
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

        1. Create a pool with --nsvc=3. There will be three ranks with rdb-pool.
        2. Stop servers.
        3. Remove <scm_mount>/<pool_uuid>/rdb-pool from two ranks.
        4. Run DAOS checker under kinds of mode.
        5. Try creating a container. The pool can be started now, so create should succeed.
        6. Show that rdb-pool are recovered. i.e., at least three out of four ranks
        should have rdb-pool.

        Jira ID: DAOS-12029

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_list_consolidation
        :avocado: tags=PoolListConsolidationTest,test_lost_majority_ps_replicas
        """
        hosts = list(set(self.server_managers[0].ranks.values()))
        md_on_ssd = self.server_managers[0].manager.job.using_control_metadata

        self.log_step("Create a pool with --nsvc=3.")
        # We can generalize this test more. For example, use
        # svcn = self.server_managers[0].engines - 1
        # Then remove (svcn / 2 + 1) count of rdb-pool, etc. However, I don't think it's
        # necessary to increase the number of servers for this test. Also, I'm not sure
        # if --nsvc > 3 will work. Thus, we keep the numbers hard-coded to make the code
        # simple.
        pool = self.get_pool(svcn=3)

        # This is where we store the original rdb-pool paths that are created during dmg pool
        # create. e.g.,
        # /mnt/daos1/$POOL/rdb-pool
        orig_rdb_pool_paths = []

        self.log_step("Stop servers")
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        if md_on_ssd:
            # MD-on-SSD case is more complex that PMEM case because we need to first load the pool
            # dir to the new mount points. Then we'll iterate the new mount points to search for
            # rdb-pool. Removing rdb-pool is also more complex than PMEM because in PMEM, we'll just
            # use rm <rdb-pool_path>, but in MD-on-SSD, we need to use "ddb rm_pool" and it takes
            # db_path, so we need to keep the mapping between rdb-pool path and db_path.

            # Prepare dictionaries to determine the arguments used in prov_mem and rm_pool.
            # Used for --db_path in prov_mem and rm_pool.
            db_path_0 = os.path.join(self.log_dir, "control_metadata", "daos_control", "engine0")
            db_path_1 = os.path.join(self.log_dir, "control_metadata", "daos_control", "engine1")

            # Create the map of original mount points to the new mount points where the pool will be
            # loaded.
            tmpfs_mounts = ["/mnt/daos2", "/mnt/daos3"]
            orig_load_mount = {}
            for i, engine_params in enumerate(
                self.server_managers[0].manager.job.yaml.engine_params):
                scm_mount = engine_params.get_value('scm_mount')
                orig_load_mount[scm_mount] = tmpfs_mounts[i]
            self.log.info("orig_load_mount = %s", orig_load_mount)

            # When we call rm_pool, we need to know the right --db_path value for a given rdb-pool
            # path to remove, so prepare an intermediate dictionary.
            mount_to_db_path = {tmpfs_mounts[0]: db_path_0, tmpfs_mounts[1]: db_path_1}
            self.log.info("mount_to_db_path = %s", mount_to_db_path)

            # This is where we store the new rdb-pool paths in the loaded dir. e.g.,
            # /mnt/daos3/$POOL/rdb-pool
            new_rdb_pool_paths = []

            # When we call rm_pool, we'll check whether a particular rdb-pool path exists. If found,
            # we'll use rm_pool to remove it. When we call the command, we also need to know db_path
            # that maps to this rdb-pool path, so prepare a dictionary. e.g.,
            # /mnt/daos3/$POOL/rdb-pool: /var/tmp/daos_testing/control_metadata/daos_control/engine1
            new_rdb_to_db_path = {}

            for engine_params in self.server_managers[0].manager.job.yaml.engine_params:
                # Iterate the server config and get the scm_mount values. Usually /mnt/daos0 and
                # /mnt/daos1.
                scm_mount = engine_params.get_value('scm_mount')

                # Determine the new rdb-pool path from the new scm_mount. e.g., If the new scm_mount
                # is /mnt/daos1, new rdb-pool path would be /mnt/daos3/$POOL/rdb-pool
                new_mount = orig_load_mount[scm_mount]

                # Create the mapping between the new rdb-pool path and the associated db_path. For
                # example, if scm_mount is /mnt/daos1, the mapping would be
                # /mnt/daos1/$POOL/rdb-pool:
                # /var/tmp/daos_testing/control_metadata/daos_control/engine1
                new_rdb_pool_path = f"{new_mount}/{pool.uuid.lower()}/rdb-pool"
                new_rdb_pool_paths.append(new_rdb_pool_path)
                orig_rdb_pool_path = f"{scm_mount}/{pool.uuid.lower()}/rdb-pool"
                # orig_rdb_pool_paths is needed when we check if rdb-pools are recovered at the end.
                orig_rdb_pool_paths.append(orig_rdb_pool_path)
                new_rdb_to_db_path[new_rdb_pool_path] = mount_to_db_path[new_mount]

            self.log.info("new_rdb_pool_paths = %s", new_rdb_pool_paths)
            self.log.info("rdb_to_db_path = %s", new_rdb_to_db_path)

            msg = "MD-on-SSD: Create a directory to load pool data under /mnt in all servers."
            self.log_step(msg)
            command = "mkdir -p /mnt/daos2 /mnt/daos3"
            command_root = command_as_user(command=command, user="root")
            result = run_remote(log=self.log, hosts=self.hostlist_servers, command=command_root)
            if not result.passed:
                self.fail(f"{command} failed on {result.failed_hosts}!")

            self.log_step("MD-on-SSD: Load pool dir to /mnt/daos2 and daos3 for all servers.")
            for host in hosts:
                # We need to call ddb prov_mem for all servers, so use new DdbCommand object with
                # each host.
                ddb_command = DdbCommand(server_host=host, path=self.bin, vos_path='""')
                ddb_command.prov_mem(db_path=db_path_0, tmpfs_mount=tmpfs_mounts[0])
                ddb_command.prov_mem(db_path=db_path_1, tmpfs_mount=tmpfs_mounts[1])

            self.log_step("Remove rdb-pool from 2 out of 3 ranks from /mnt/daos2 and /mnt/daos3")
            count = 0
            # Iterate both pool mount points of both ranks. I.e., 4 ranks total.
            for host in hosts:
                for rdb_pool_path in new_rdb_pool_paths:
                    node = NodeSet(host)
                    # rdb_pool_path is something like "/mnt/daos2/$POOL/rdb-pool". Check if it
                    # exists in this host.
                    check_out = check_file_exists(hosts=node, filename=rdb_pool_path, sudo=True)
                    if check_out[0]:
                        # As in prov_mem, we're calling ddb rm_pool in this particular host, so use
                        # a new object with this particular host.
                        ddb_command = DdbCommand(server_host=host, path=self.bin, vos_path=None)
                        # Get the corresponding db_path from the rdb-pool path we're removing.
                        db_path = new_rdb_to_db_path[rdb_pool_path]
                        self.log.info("Remove %s from %s", rdb_pool_path, str(node))
                        ddb_command.rm_pool(db_path=db_path, removing_path=rdb_pool_path)
                        count += 1
                        if count == 2:
                            break
                if count == 2:
                    break

        else:
            # PMEM case.
            self.log_step("Remove <scm_mount>/<pool_uuid>/rdb-pool from two ranks.")
            for engine_params in self.server_managers[0].manager.job.yaml.engine_params:
                scm_mount = engine_params.get_value('scm_mount')
                rdb_pool_path = f"{scm_mount}/{pool.uuid.lower()}/rdb-pool"
                orig_rdb_pool_paths.append(rdb_pool_path)
            self.log.info("orig_rdb_pool_paths = %s", orig_rdb_pool_paths)
            count = 0
            # Iterate both pool mount points of both ranks. I.e., 4 ranks total.
            for host in hosts:
                for rdb_pool_path in orig_rdb_pool_paths:
                    node = NodeSet(host)
                    check_out = check_file_exists(hosts=node, filename=rdb_pool_path, sudo=True)
                    if check_out[0]:
                        command = f"rm {rdb_pool_path}"
                        command_root = command_as_user(command=command, user="root")
                        if not run_remote(log=self.log, hosts=node, command=command_root).passed:
                            self.fail(f'Failed to remove {rdb_pool_path} on {host}')
                        self.log.info("Remove %s from %s", rdb_pool_path, str(node))
                        count += 1
                        if count == 2:
                            break
                if count == 2:
                    break

        self.log_step("Run DAOS checker under kinds of mode.")
        errors = []
        errors = self.chk_dist_checker(inconsistency="corrupted pool without quorum")

        self.log_step("Try creating a container. It should succeed.")
        cont_create_success = False
        for _ in range(5):
            time.sleep(5)
            try:
                self.get_container(pool)
                cont_create_success = True
                break
            except TestFail as error:
                msg = f"Container create failed after running checker! error = {error}"
                self.log.debug(msg)

        if not cont_create_success:
            errors.append("Container create failed after running checker!")

        msg = ("Show that rdb-pool are recovered. i.e., three out of four ranks should "
               "have rdb-pool.")
        self.log_step(msg)
        count = 0
        for host in hosts:
            for rdb_pool_path in orig_rdb_pool_paths:
                node = NodeSet(host)
                check_out = check_file_exists(hosts=node, filename=rdb_pool_path, sudo=True)
                if check_out[0]:
                    count += 1
                    self.log.info("rdb-pool found at %s: %s", str(node), rdb_pool_path)

        self.log.info("rdb-pool count = %d", count)
        if count != 3:
            errors.append(f"Unexpected number of rdb-pool after repair! - {count} ranks")

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
        rdb_pool_exists = [check_file_exists(
            self.hostlist_servers, path, sudo=True)[0] for path in rdb_pool_paths]
        if not all(rdb_pool_exists):
            msg = ("MD-on-SSD cluster. Contents under mount point are removed by control plane "
                   "after system stop.")
            self.log.info(msg)
            dmg_command.system_start()
            # return results in PASS.
            return
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
