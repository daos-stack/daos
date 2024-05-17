"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from ClusterShell.NodeSet import NodeSet
from general_utils import check_file_exists, report_errors
from ior_test_base import IorTestBase
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

    def wait_for_check_complete(self):
        """Repeatedly call dmg check query until status becomes COMPLETED.

        If the status doesn't become COMPLETED, fail the test.

        Returns:
            list: List of repair reports.

        """
        repair_reports = None
        for _ in range(8):
            check_query_out = self.get_dmg_command().check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                repair_reports = check_query_out["response"]["reports"]
                break
            time.sleep(5)

        if not repair_reports:
            self.fail("Checker didn't detect or repair any inconsistency!")

        return repair_reports

    def test_orphan_pool_shard(self):
        """Test orphan pool shard.

        1. Create a pool on rank 0.
        2. Call dmg storage query usage to store the default space utilization.
        3. Prepare to copy the pool path before stopping servers.
        4. Stop servers.
        5. Copy /mnt/daos?/<pool_path> from the engine where we created the pool to
        another engine where we didn’t create. Destination engine is in different node.
        6. Enable and start the checker.
        7. Query the checker and verify that the issue was fixed.
        i.e., Current status is COMPLETED.
        8. Disable the checker and start server.
        9. Call dmg storage query usage to verify that the pool usage is back to the
        original value.

        Jira ID: DAOS-11734

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,pool_membership
        :avocado: tags=PoolMembershipTest,test_orphan_pool_shard
        """
        # 1. Create a pool.
        self.log_step("Creating a pool (dmg pool create)")
        pool = self.get_pool(connect=False, target_list="0")

        # 2. Call dmg storage query usage to store the default space utilization.
        self.log_step("Collecting free space for each rank (dmg storage query usage)")
        rank_to_free_orig = self.get_rank_to_free()
        self.log.info("rank_to_free_orig = %s", rank_to_free_orig)

        # 3. Prepare to copy the pool path before stopping servers.

        # In order to copy the pool directory without password, there are several things
        # to determine and set up.

        # 3-1. Determine source host and destination host. Source host is where rank 0 is.
        # Destination host is the other host.
        self.log_step("Determine source host and destination host.")
        src_host = dst_host = NodeSet(self.server_managers[0].get_host(0))
        rank = 1
        while rank < self.server_managers[0].engines and dst_host == src_host:
            dst_host = NodeSet(self.server_managers[0].get_host(rank))
            rank += 1
        self.log.info("src_host = %s; dst_host = %s", src_host, dst_host)

        # 3-2. Determine source and destination mount point. First, find the source mount
        # point that maps to rank 0. Then use the same mount point for the destination.
        # This way, we can handle the case where the mount point name is changed in the
        # future. At the same time, determine the destination rank, which is where
        # dst_mount is mapped.
        src_mount = None
        dst_mount = None
        dst_rank = None
        dmg_command = self.get_dmg_command()
        storage_query_out = dmg_command.storage_query_usage()
        hash_dict = storage_query_out["response"]["HostStorage"]
        for storage_dict in hash_dict.values():
            if str(src_host) in storage_dict["hosts"]:
                # Determine source mount point that maps to rank 0.
                for scm_namespace in storage_dict["storage"]["scm_namespaces"]:
                    if scm_namespace["mount"]["rank"] == 0:
                        # For dst_mount, use the same mount point as source.
                        dst_mount = src_mount = scm_namespace["mount"]["path"]
        for storage_dict in hash_dict.values():
            if str(dst_host) in storage_dict["hosts"]:
                # Determine destination rank that maps to dst_mount.
                for scm_namespace in storage_dict["storage"]["scm_namespaces"]:
                    if scm_namespace["mount"]["path"] == dst_mount:
                        dst_rank = scm_namespace["mount"]["rank"]

        # 4. Stop servers.
        self.log_step("Stop servers.")
        self.server_managers[0].system_stop()

        scm_mount_result = check_file_exists(
            hosts=self.hostlist_servers, filename=src_mount, directory=True)
        if not scm_mount_result[0]:
            msg = "MD-on-SSD cluster. Mount point is removed by control plane after system stop."
            self.log.info(msg)
            dmg_command.system_start()
            # return results in PASS.
            return

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
        self.log_step(
            f"Copy pool files from {src_host}:{src_mount} to {dst_host}:{dst_mount}.")
        xargs_rsync_cmd = (f"ls {src_mount}/{pool.uuid.lower()} | "
                           f"xargs --max-procs=8 -I% "
                           f"rsync -avz {src_mount}/{pool.uuid.lower()}/% "
                           f"{str(dst_host)}:{dst_mount}/{pool.uuid.lower()}")
        if not run_remote(log=self.log, hosts=src_host, command=xargs_rsync_cmd).passed:
            self.fail(f"Following command failed on {src_host}! {xargs_rsync_cmd}")

        # 6. Enable and start the checker.
        self.log_step("Enable and start the checker.")
        dmg_command.check_enable()

        # If we call check start immediately after check enable, checker may not detect
        # the fault. Developer is fixing this issue.
        time.sleep(3)

        dmg_command.check_start()

        # 7. Query the checker and verify that the issue was fixed.
        # i.e., Current status is COMPLETED.
        errors = []
        repair_reports = self.wait_for_check_complete()
        query_msg = repair_reports[0]["msg"]
        if "orphan rank" not in query_msg:
            errors.append(
                "Checker didn't fix orphan pool shard! msg = {}".format(query_msg))

        # 8. Disable the checker and start server.
        self.log_step("Disable the checker and start server.")
        dmg_command.check_disable()

        # 9. Call dmg storage query usage to verify that the pool usage is back to the
        # original value.
        self.log_step("Verify that the pool usage is back to the original value.")
        rank_to_free_fixed = self.get_rank_to_free()
        self.log.info("rank_to_free_fixed = %s", rank_to_free_fixed)
        dst_free_orig = rank_to_free_orig[dst_rank]
        dst_free_fixed = rank_to_free_fixed[dst_rank]
        # Free space should have been recovered to the original value. If not, bring it up
        # in the CR working group.
        if dst_free_fixed < dst_free_orig:
            msg = (f"Destination rank space was not recovered by checker! "
                   f"Original = {dst_free_orig}; With fixed = {dst_free_fixed}")
            errors.append(msg)

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
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        scm_mount_result = check_file_exists(
            hosts=self.hostlist_servers, filename=scm_mount, directory=True)
        if not scm_mount_result[0]:
            msg = "MD-on-SSD cluster. Mount point is removed by control plane after system stop."
            self.log.info(msg)
            dmg_command.system_start()
            # return results in PASS.
            return
        rm_cmd = f"sudo rm {scm_mount}/{pool.uuid.lower()}/vos-0"
        if not run_remote(log=self.log, hosts=rank_0_host, command=rm_cmd).passed:
            self.fail(f"Following command failed on {rank_0_host}! {rm_cmd}")

        self.log_step("Enable and start the checker.")
        dmg_command.check_enable(stop=False)

        # If we call check start immediately after check enable, checker may not detect
        # the fault. Developer is fixing this issue.
        time.sleep(3)

        dmg_command.check_start()

        self.log_step("Query the checker and verify that the issue was fixed.")
        repair_reports = self.wait_for_check_complete()

        errors = []
        query_msg = repair_reports[0]["msg"]
        if "dangling target" not in query_msg:
            errors.append(
                "Checker didn't fix orphan pool shard! msg = {}".format(query_msg))

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
        targets = self.params.get("targets", "/run/server_config/engines/0/*")
        exp_msg = "dangling rank entry"

        self.log_step("Create a pool and a container.")
        self.pool = self.get_pool(connect=False)
        self.container = self.get_container(pool=self.pool)

        self.log_step("Write some data with IOR.")
        self.ior_cmd.set_daos_params(self.pool, self.container.identifier)
        self.run_ior_with_pool(create_pool=False, create_cont=False)

        self.log_step("Stop servers.")
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        self.log_step("Remove pool directory from one of the mount points.")
        rank_1_host = NodeSet(self.server_managers[0].get_host(1))
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        scm_mount_result = check_file_exists(
            hosts=self.hostlist_servers, filename=scm_mount, directory=True)
        if not scm_mount_result[0]:
            msg = "MD-on-SSD cluster. Mount point is removed by control plane after system stop."
            self.log.info(msg)
            dmg_command.system_start()
            # return results in PASS.
            return
        rm_cmd = f"sudo rm -rf {scm_mount}/{self.pool.uuid.lower()}"
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
        repair_reports = self.wait_for_check_complete()

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
