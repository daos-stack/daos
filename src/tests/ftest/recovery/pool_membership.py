"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import shlex
import time

from ClusterShell.NodeSet import NodeSet
from general_utils import check_file_exists, report_errors
from ior_test_base import IorTestBase
from recovery_utils import wait_for_check_complete
from run_utils import run_remote


class PoolMembershipTest(IorTestBase):
    """Test Pass 2: Pool Membership

    :avocado: recursive
    """

    def get_rank_to_free(self):
        """Get the free space for each engine rank.

        Call dmg storage query usage for all servers and return free space (avail_bytes)
        for each rank as dictionary.

        Returns:
            dict: Key is rank and value is free space of the rank in bytes as int.

        """
        storage_query_out = self.server_managers[0].dmg.storage_query_usage()
        rank_to_free = {}
        storage_list = storage_query_out["response"]["HostStorage"]
        for storage_hash in storage_list.values():
            for scm_namespace in storage_hash["storage"]["scm_namespaces"]:
                rank = scm_namespace["mount"]["rank"]
                free = scm_namespace["mount"]["avail_bytes"]
                rank_to_free[rank] = free

        return rank_to_free

    def get_pmem_pool_copy_locations(self, pool_rank):
        """Find the hosts, mounts, and destination rank for copying a PMEM pool.

        Args:
            pool_rank (int): Rank containing the source pool.

        Returns:
            tuple: Source host, destination host, source mount, destination mount, and
                destination rank.
        """
        self.log_step("Determine source host and destination host.")
        src_host = dst_host = NodeSet(self.server_managers[0].get_host(pool_rank))
        rank = 1
        while rank < self.server_managers[0].engines and dst_host == src_host:
            dst_host = NodeSet(self.server_managers[0].get_host(rank))
            rank += 1
        self.log.info("src_host = %s; dst_host = %s", src_host, dst_host)

        src_mount = None
        dst_mount = None
        dst_rank = None
        storage_query_out = self.get_dmg_command().storage_query_usage()
        hash_dict = storage_query_out["response"]["HostStorage"]
        for storage_dict in hash_dict.values():
            if str(src_host) in storage_dict["hosts"]:
                for scm_namespace in storage_dict["storage"]["scm_namespaces"]:
                    if scm_namespace["mount"]["rank"] == pool_rank:
                        dst_mount = src_mount = scm_namespace["mount"]["path"]
        for storage_dict in hash_dict.values():
            if str(dst_host) in storage_dict["hosts"]:
                for scm_namespace in storage_dict["storage"]["scm_namespaces"]:
                    if scm_namespace["mount"]["path"] == dst_mount:
                        dst_rank = scm_namespace["mount"]["rank"]

        return src_host, dst_host, src_mount, dst_mount, dst_rank

    def verify_pmem_pool_copy(self, pool, src_host, src_mount, dst_host, dst_mount):
        """Verify copied PMEM pool filenames, sizes, and expected RDB/VOS files.

        Args:
            pool (TestPool): Pool copied to create the orphan shard.
            src_host (NodeSet): Source host.
            src_mount (str): Source engine mount point.
            dst_host (NodeSet): Destination host.
            dst_mount (str): Destination engine mount point.
        """
        for engine in self.server_managers[0].manager.job.yaml.engine_params:
            if engine.get_value("scm_mount") == src_mount:
                targets = engine.get_value("targets")
                break
        else:
            self.fail(f"No engine configuration found for source mount {src_mount}")

        expected_files = {"rdb-pool"} | {f"vos-{index}" for index in range(targets)}
        manifests = []
        for host, mount in ((src_host, src_mount), (dst_host, dst_mount)):
            pool_path = f"{mount}/{pool.uuid.lower()}"
            command = (
                f"find {shlex.quote(pool_path)} -maxdepth 1 -type f -printf '%f\\t%s\\n'")
            result = run_remote(log=self.log, hosts=host, command=command)
            if not result.passed:
                self.fail(
                    f"Failed to list pool files at {host}:{pool_path}: "
                    f"stdout={result.all_stdout}, stderr={result.all_stderr}")
            manifest = {}
            for line in result.joined_stdout.splitlines():
                filename, size = line.rsplit("\t", 1)
                manifest[filename] = int(size)
            self.log.info("Pool file sizes at %s:%s: %s", host, pool_path, manifest)
            missing = expected_files - manifest.keys()
            if missing:
                self.fail(f"Missing pool files at {host}:{pool_path}: {sorted(missing)}")
            manifests.append(manifest)

        self.assertEqual(
            manifests[0], manifests[1],
            f"Pool copy filenames or sizes differ between {src_host}:{src_mount} "
            f"and {dst_host}:{dst_mount}")

    def test_orphan_pool_shard(self):
        """Test orphan pool shard.

        PMEM test steps:
        1. Create a pool on rank 0.
        2. Prepare to copy the pool path before stopping servers.
        3. Stop servers.
        4. Copy /mnt/daos?/<pool_path> from the engine where we created the pool to
        another engine where we didn’t create. Destination engine is in different node.
        5. Enable and start the checker.
        6. Query the checker and verify that the issue was fixed. i.e., Current status is COMPLETED.
        7. Disable the checker and start server.
        8. Check that orphan pool shard was removed.

        MD-on-SSD test steps:
        1. Inject orphan pool shard to rank 3 using daos faults.
        2. Create a pool on a rank on a different host from rank 3.
        3. Check that orphan pool shard was created on rank 3 mount.
        4. Stop servers.
        5. Enable and start the checker.
        6. Query the checker and verify that the issue was fixed. i.e., Current status is COMPLETED.
        7. Disable the checker and start server.
        8. Check that orphan pool shard was removed.

        PMEM Jira ID: DAOS-11734
        MD-on-SSD Jira ID: DAOS-18397

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_membership
        :avocado: tags=PoolMembershipTest,test_orphan_pool_shard
        """
        # Test steps for PMEM and MD-on-SSD are quite different. For PMEM, we copy the pool dir from
        # the engine where we created the pool to another engine where we didn't create. We need to
        # create a new directory, set mode, copy while considering the speed, etc. For MD-on-SSD, we
        # use daos faults and that does the copying for us, so the steps are a lot simpler.
        md_on_ssd = self.server_managers[0].manager.job.using_control_metadata

        # MD-on-SSD variables.
        rank_to_mount = {}
        fault_rank = 3

        dmg_command = self.get_dmg_command()

        orphan_pool_host = None
        orphan_pool_path = None
        pool_rank = 0

        if md_on_ssd:
            self.log_step(
                f"MD-on-SSD: Inject orphan pool shard to rank {fault_rank} using daos faults.")
            self.get_daos_command().faults_set_param(
                location="DAOS_CHK_ORPHAN_POOL_SHARD", value=str(fault_rank))

            storage_query_out = dmg_command.storage_query_usage()
            hash_dict = storage_query_out["response"]["HostStorage"]
            for storage_dict in hash_dict.values():
                for scm_namespace in storage_dict["storage"]["scm_namespaces"]:
                    rank_to_mount[scm_namespace["mount"]["rank"]] = scm_namespace["mount"]["path"]

            orphan_pool_host = self.server_managers[0].get_host(fault_rank)
            for rank in sorted(rank_to_mount):
                if (rank != fault_rank
                        and self.server_managers[0].get_host(rank) != orphan_pool_host):
                    pool_rank = rank
                    break
            else:
                self.fail(f"No pool rank found on a different host from rank {fault_rank}!")

            self.log.info("MD-on-SSD: rank_to_mount %s", rank_to_mount)

        self.log_step(f"Create a pool on rank {pool_rank}.")
        pool = self.get_pool(connect=False, target_list=str(pool_rank))

        # PMEM variables. PMEM has quite different algorithm, so these names work better.
        src_host = None
        dst_host = None
        src_mount = None
        dst_mount = None
        dst_rank = None

        if md_on_ssd:
            orphan_pool_path = f"{rank_to_mount[fault_rank]}/{pool.uuid.lower()}"
            self.log_step("Check that orphan pool shard was created on rank 3 mount.")
            orphan_pool_result = check_file_exists(
                hosts=NodeSet(orphan_pool_host), filename=orphan_pool_path, directory=True)
            if not orphan_pool_result[0]:
                self.fail(
                    f"Orphan pool wasn't created on rank {fault_rank} on "
                    f"{rank_to_mount[fault_rank]} at {orphan_pool_host}!")
        else:
            src_host, dst_host, src_mount, dst_mount, dst_rank = (
                self.get_pmem_pool_copy_locations(pool_rank))

        self.log_step("Stop servers.")
        dmg_command.system_stop()

        if not md_on_ssd:
            # 5. Copy /mnt/daos?/<pool_path> from the engine where we created the pool to
            # another engine where we didn’t create.

            # 5-1. Since we're running rsync as user, update the mode of the source pool
            # directory to 777.
            self.log_step("Update mode of the source pool directory.")
            chmod_cmd = (f"sudo chmod 777 {src_mount}; "
                         f"sudo chmod -R 777 {src_mount}/{pool.uuid.lower()}")
            if not run_remote(log=self.log, hosts=src_host, command=chmod_cmd).passed:
                self.fail(f"Following command failed on {dst_host}! {chmod_cmd}")

            # 5-2. Update mode of the destination mount point to 777 so that we can send the
            # pool files.
            self.log_step("Update mode of the destination mount point.")
            chmod_cmd = f"sudo chmod 777 {dst_mount}"
            if not run_remote(log=self.log, hosts=dst_host, command=chmod_cmd).passed:
                self.fail(f"Following command failed on {dst_host}! {chmod_cmd}")

            # 5-3. Since we're sending each file (vos-0 to 7 + rdb-pool) one at a time, we need
            # to create the destination fake pool directory first.
            self.log_step("Create a fake pool directory at the destination mount point.")
            mkdir_cmd = f"sudo mkdir {dst_mount}/{pool.uuid.lower()}"
            if not run_remote(log=self.log, hosts=dst_host, command=mkdir_cmd).passed:
                self.fail(f"Following command failed on {dst_host}! {mkdir_cmd}")

            # 5-4. Update mode of the destination pool directory to 777 so that we can send
            # the pool files.
            self.log_step("Update mode of the fake pool directory at destination.")
            chmod_cmd = f"sudo chmod 777 {dst_mount}/{pool.uuid.lower()}"
            if not run_remote(log=self.log, hosts=dst_host, command=chmod_cmd).passed:
                self.fail(f"Following command failed on {dst_host}! {chmod_cmd}")

            # 5-5. Send the files.
            # 1. The initial ls command lists the content of the pool directory, which
            # contains 8 vos files (because there are 8 targets) and rdb-pool file.
            # 2. By using xargs, each item of the ls output is passed into rsync and the rsync
            # commands are executed in parallel. i.e., each file is sent by separate rsync
            # process in parallel.

            # - More explanations about the command:
            # * We use --max-procs=8 to support at most 8 rsync processes to run in parallel.
            # * -I% means replace % in the following rsync command by the output of ls. i.e.,
            # file name.
            # * rsync -avz means archive, verbose, and compress. By using compress, we can
            # significantly reduce the size of the data and the transfer time.
            # * By running rsync in parallel, we can significantly reduce the transfer time.
            # e.g., For a 2TB pool with 8 targets per engine, each vos file size is about 7G
            # (rdb-pool is smaller). If we run a simple rsync, which runs serially, it takes
            # 1 min 50 sec. However, if we run them in parallel, it's reduced to 24 sec.
            self.log_step(f"Copy pool files from {src_host}:{src_mount} to {dst_host}:{dst_mount}.")
            xargs_rsync_cmd = (
                f"ls {src_mount}/{pool.uuid.lower()} | "
                f"xargs --max-procs=8 -I% "
                f"rsync -avz {src_mount}/{pool.uuid.lower()}/% "
                f"{str(dst_host)}:{dst_mount}/{pool.uuid.lower()}")
            if not run_remote(log=self.log, hosts=src_host, command=xargs_rsync_cmd).passed:
                self.fail(f"Following command failed on {src_host}! {xargs_rsync_cmd}")

            self.log_step("Verify copied pool filenames, sizes, and expected RDB/VOS files.")
            self.verify_pmem_pool_copy(pool, src_host, src_mount, dst_host, dst_mount)

        self.log_step("Enable and start the checker.")
        dmg_command.check_enable()

        # If we call check start immediately after check enable, checker may not detect
        # the fault. Developer is fixing this issue.
        time.sleep(3)

        dmg_command.check_start()

        self.log_step("Query the checker and verify that the issue was fixed.")
        errors = []
        repair_reports = wait_for_check_complete(dmg_command)
        if not repair_reports:
            self.fail("Checker repair report not found!")
        query_msg = repair_reports[0]["msg"]
        if "orphan rank" not in query_msg:
            errors.append(f"Checker didn't fix orphan pool shard! msg = {query_msg}")

        self.log_step("Disable the checker and start server.")
        dmg_command.check_disable()

        if md_on_ssd:
            self.log_step("MD-on-SSD: Check that orphan pool shard was removed.")
            orphan_pool_result = check_file_exists(
                hosts=NodeSet(orphan_pool_host), filename=orphan_pool_path, directory=True)
            if orphan_pool_result[0]:
                self.fail(
                    f"Orphan pool wasn't removed on rank {fault_rank} on "
                    f"{rank_to_mount[fault_rank]} at {orphan_pool_host}!")
        else:
            self.log_step("PMEM: Check that orphan pool shard was removed.")
            orphan_pool_path = f"{dst_mount}/{pool.uuid.lower()}"
            orphan_pool_result = check_file_exists(
                hosts=NodeSet(dst_host), filename=orphan_pool_path, directory=True)
            if orphan_pool_result[0]:
                self.fail(
                    f"Orphan pool wasn't removed on rank {dst_rank} on {dst_mount} at {dst_host}!")

        self.log_step("Verify checker didn't destroy the pool and it still exists.")
        pool_list_out = dmg_command.pool_list()
        pools = pool_list_out["response"]["pools"]
        if pools:
            out_label = pools[0]["label"]
            self.assertEqual(
                out_label, pool.identifier, "Pool labels don't match after check repair!")
        else:
            errors.append("Pool wasn't found after check repair!")

        report_errors(test=self, errors=errors)

    def test_dangling_pool_map(self):
        """Test dangling pool map.

        1. Create a pool.
        2. Stop servers.
        3. Manually remove /<scm_mount>/<pool_uuid>/vos-0 from rank 0 node.
        4. Enable and start the checker.
        5. Query the checker and verify that the issue was fixed. i.e., Current status is
        COMPLETED.
        6. Disable the checker and start server.
        7. Verify that the pool has one less target.

        Jira ID: DAOS-11736

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_membership
        :avocado: tags=PoolMembershipTest,test_dangling_pool_map
        """
        self.log_step("Create a pool.")
        pool = self.get_pool(connect=False)

        self.log_step("Stop servers.")
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        self.log_step("Manually remove /<scm_mount>/<pool_uuid>/vos-0 from rank 0 node.")
        rank_0_host = NodeSet(self.server_managers[0].get_host(0))
        vos_0_path = f"{self.server_managers[0].get_vos_paths(pool)[0]}/vos-0"
        vos_0_result = check_file_exists(hosts=self.hostlist_servers, filename=vos_0_path)
        if not vos_0_result[0]:
            msg = ("MD-on-SSD cluster. Contents under mount point are removed by control plane "
                   "after system stop.")
            self.log.info(msg)
            dmg_command.system_start()
            # return results in PASS.
            return
        rm_cmd = f"sudo rm {vos_0_path}"
        if not run_remote(log=self.log, hosts=rank_0_host, command=rm_cmd).passed:
            self.fail(f"Following command failed on {rank_0_host}! {rm_cmd}")

        self.log_step("Enable and start the checker.")
        dmg_command.check_enable(stop=False)

        # If we call check start immediately after check enable, checker may not detect
        # the fault. Developer is fixing this issue.
        time.sleep(3)

        dmg_command.check_start()

        self.log_step("Query the checker and verify that the issue was fixed.")
        repair_reports = wait_for_check_complete(dmg_command)

        errors = []
        query_msg = repair_reports[0]["msg"]
        if "dangling target" not in query_msg:
            errors.append(f"Checker didn't fix orphan pool shard! msg = {query_msg}")

        self.log_step("Disable the checker and start server.")
        dmg_command.check_disable()

        self.log_step("Verify that the pool has one less target.")
        query_out = pool.query()
        total_targets = query_out["response"]["total_targets"]
        active_targets = query_out["response"]["active_targets"]
        expected_targets = total_targets - 1
        if active_targets != expected_targets:
            msg = (f"Unexpected number of active targets! Expected = {expected_targets}; "
                   f"Actual = {active_targets}")
            errors.append(msg)

        report_errors(test=self, errors=errors)

    def test_dangling_rank_entry(self):
        """Test dangling target entry.

        1. Create a pool and a container.
        2. Write some data with IOR using SX.
        3. Stop servers.
        4. Remove pool directory from one of the mount points.
        5. Enable checker.
        6. Start checker.
        7. Query the checker until expected number of inconsistencies are repaired.
        8. Disable checker and start servers.

        Jira ID: DAOS-11735

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_membership
        :avocado: tags=PoolMembershipTest,test_dangling_rank_entry
        """
        if self.server_managers[0].manager.job.using_control_metadata:
            self.log.info("MD-on-SSD cluster. Will be supported later.")
            # return results in PASS.
            return

        targets = self.params.get("targets", "/run/server_config/engines/0/*")
        exp_msg = "dangling rank entry"

        self.log_step("Create a pool and a container.")
        self.pool = self.get_pool(connect=False)
        self.container = self.get_container(self.pool)

        self.log_step("Write some data with IOR.")
        self.ior_cmd.set_daos_params(self.pool, self.container.identifier)
        self.run_ior_with_pool(create_pool=False, create_cont=False)

        self.log_step("Stop servers.")
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        self.log_step("Remove pool directory from one of the mount points.")
        rank_1_host = NodeSet(self.server_managers[0].get_host(1))
        pool_directory = self.server_managers[0].get_vos_paths(self.pool)[0]
        pool_directory_result = check_file_exists(
            hosts=self.hostlist_servers, filename=pool_directory, directory=True)
        if not pool_directory_result[0]:
            msg = ("MD-on-SSD cluster. Contents under mount point are removed by control plane "
                   "after system stop.")
            self.log.info(msg)
            dmg_command.system_start()
            # return results in PASS.
            return
        rm_cmd = f"sudo rm -rf {pool_directory}"
        if not run_remote(log=self.log, hosts=rank_1_host, command=rm_cmd).passed:
            self.fail(f"Following command failed on {rank_1_host}! {rm_cmd}")

        self.log_step("Enable checker.")
        dmg_command.check_enable(stop=False)

        # If we call check start immediately after check enable, checker may not detect
        # the fault. Developer is fixing this issue.
        time.sleep(3)

        self.log_step("Start checker.")
        dmg_command.check_start()

        self.log_step(
            "Query the checker until expected number of inconsistencies are repaired.")
        repair_reports = wait_for_check_complete(dmg_command)

        # Verify that the checker repaired target count + 1 faults. (+1 is for rank.
        # Checker marks it as down.)
        errors = []
        repair_count = len(repair_reports)
        expected_repair_count = targets + 1
        if repair_count != expected_repair_count:
            msg = (f"Unexpected number of repair count! Expected = "
                   f"{expected_repair_count}, Actual = {repair_count}")
            errors.append(msg)

        # Verify that the message contains dangling rank entry.
        exp_msg_found = False
        for repair_report in repair_reports:
            if exp_msg in repair_report["msg"]:
                exp_msg_found = True
                break
        if not exp_msg_found:
            errors.append(f"{exp_msg} not in repair message!")

        self.log_step("Disable checker.")
        dmg_command.check_disable()

        report_errors(test=self, errors=errors)
