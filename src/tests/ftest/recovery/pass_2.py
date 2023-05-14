"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from ClusterShell.NodeSet import NodeSet

from apricot import TestWithServers
from general_utils import report_errors, pcmd


class Pass2Test(TestWithServers):
    """Test Pass 2: Pool Membership

    :avocado: recursive
    """

    def get_rank_to_free(self):
        """Call dmg storage query usage for all servers and return free space
        (avail_bytes) for each rank as dictionary.

        Returns:
            dict: Key is rank and value is free space of the rank in bytes as int.

        """
        host_list = str(self.hostlist_servers)
        storage_query_out = self.get_dmg_command().storage_query_usage(
            host_list=host_list)
        rank_to_free = {}
        storage_list = storage_query_out["response"]["HostStorage"]
        for storage_hash in storage_list.values():
            for scm_namespace in storage_hash["storage"]["scm_namespaces"]:
                rank = scm_namespace["mount"]["rank"]
                free = scm_namespace["mount"]["avail_bytes"]
                rank_to_free[rank] = free

        return rank_to_free

    def test_orphan_pool_shard(self):
        """Test orphan pool shard.

        1. Create a pool on rank 0.
        2. Call dmg storage query usage to store the default space utilization.
        3. Prepare to copy the pool path before stopping servers.
        4. Stop servers.
        5. Copy /mnt/daos?/<pool_path> from the engine where we created the pool to
        another engine where we didn’t create.
        6. Start servers.
        7. Call dmg storage query usage to verify that the pool is using more space by
        comparing with the previous values.
        8. Stop the servers to enable the checker.
        9. Enable and start the checker.
        10. Query the checker and verify that the issue was fixed.
        i.e., Current status is COMPLETED.
        11. Disable the checker.
        12. Restart the servers so that the storage usage in the next step is updated.
        13. Call dmg storage query usage to verify that the pool usage is back to the
        original value.

        Jira ID: DAOS-11734

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=recovery,pass_2
        :avocado: tags=Pass2Test,test_orphan_pool_shard
        """
        # 1. Create a pool.
        self.pool = self.get_pool(connect=False, target_list="0")

        # 2. Call dmg storage query usage to store the default space utilization.
        rank_to_free_orig = self.get_rank_to_free()
        self.log.info("rank_to_free_orig = %s", rank_to_free_orig)

        # 3. Prepare to copy the pool path before stopping servers.

        # In order to copy the pool directory without password, there are several things
        # to determine and set up.

        # 3-1. Determine source host and destination host. Source host is where rank 0 is.
        # Destination host is the other host.
        rank_to_host = self.server_managers[0].ranks
        self.log.info("rank_to_host = %s", rank_to_host)
        src_host = NodeSet(rank_to_host[0])
        dst_host = None
        for rank in range(1, 4):
            if rank_to_host[rank] != str(src_host):
                dst_host = NodeSet(rank_to_host[rank])
                break
        self.log.info("src_host = %s; dst_host = %s", src_host, dst_host)

        # 3-2. Determine source and destination mount point. First, find the source mount
        # point that maps to rank 0. Then use the same mount point for the destination.
        # This way, we can handle the case where the mount point name is changed in the
        # future. At the same time, determine the destination rank, which is where
        # dst_mount is mapped.
        # dst_mount = "/mnt/daos0"
        src_mount = None
        dst_mount = None
        dst_rank = None
        host_list = str(self.hostlist_servers)
        dmg_command = self.get_dmg_command()
        storage_query_out = dmg_command.storage_query_usage(
            host_list=host_list)
        hash_dict = storage_query_out["response"]["HostStorage"]
        for storage_dict in hash_dict.values():
            if str(src_host) in storage_dict["hosts"]:
                # Determine source mount point that maps to rank 0.
                for scm_namespace in storage_dict["storage"]["scm_namespaces"]:
                    if scm_namespace["mount"]["rank"] == 0:
                        src_mount = scm_namespace["mount"]["path"]
                        # Use the same mount point as source.
                        dst_mount = src_mount
        for storage_dict in hash_dict.values():
            if str(dst_host) in storage_dict["hosts"]:
                # Determine destination rank that mpas to dst_mount.
                for scm_namespace in storage_dict["storage"]["scm_namespaces"]:
                    if scm_namespace["mount"]["path"] == dst_mount:
                        dst_rank = scm_namespace["mount"]["rank"]

        # 4. Stop servers.
        dmg_command.system_stop()

        # 5. Copy /mnt/daos?/<pool_path> from the engine where we created the pool to
        # another engine where we didn’t create.

        # 5-1. Since we're running rsync as user, update the mode of the source pool
        # directory to 777.
        self.log.info("Update mode of the source pool directory.")
        chmod_cmd = (f"sudo chmod 777 {src_mount}; "
                     f"sudo chmod -R 777 {src_mount}/{self.pool.uuid.lower()}")
        pcmd(hosts=src_host, command=chmod_cmd)

        # 5-2. Update mode of the destination mount point to 777 so that we can send the
        # pool files.
        self.log.info("Update mode of the destination mount point.")
        chmod_cmd = f"sudo chmod 777 {dst_mount}"
        pcmd(hosts=dst_host, command=chmod_cmd)

        # 5-3. Since we're sending each file (vos-0 to 7 + rdb-pool) one at a time, we need
        # to create the destination fake pool directory first.
        self.log.info("Create a fake pool directory at the destination mount point.")
        mkdir_cmd = f"sudo mkdir {dst_mount}/{self.pool.uuid.lower()}"
        pcmd(hosts=dst_host, command=mkdir_cmd)

        # 5-4. Update mode of the destination pool directory to 777 so that we can send
        # the pool files.
        self.log.info("Update mode of the fake pool directory at destination.")
        chmod_cmd = f"sudo chmod 777 {dst_mount}/{self.pool.uuid.lower()}"
        pcmd(hosts=dst_host, command=chmod_cmd)

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
        self.log.info(
            "Copy pool files from %s:%s to %s:%s.", src_host, src_mount, dst_host,
            dst_mount)
        xargs_rsync_cmd = (f"ls {src_mount}/{self.pool.uuid.lower()} | "
                           f"xargs --max-procs=8 -I% "
                           f"rsync -avz {src_mount}/{self.pool.uuid.lower()}/% "
                           f"{str(dst_host)}:{dst_mount}/{self.pool.uuid.lower()}")
        pcmd(hosts=src_host, command=xargs_rsync_cmd)

        # 6. Start servers.
        dmg_command.system_start()

        # 7. Call dmg storage query usage to verify that the pool is using more space at
        # destination host by comparing with the previous values. (Verify that the fault
        # was injected correctly.)
        rank_to_free_orphan = self.get_rank_to_free()
        self.log.info("rank_to_free_orphan = %s", rank_to_free_orphan)
        dst_free_orig = rank_to_free_orig[dst_rank]
        dst_free_orphan = rank_to_free_orphan[dst_rank]
        errors = []
        # Free space should be reduced by the pool size. Add 90% of the pool size to the
        # free space. If the sum is larger than the original, conclude that the free space
        # wasn't reduced as expected.
        buffer = int(self.pool.size.value * 0.9)
        if dst_free_orphan + buffer > dst_free_orig:
            msg = (f"Pool was not copied to dst rank! Original = {dst_free_orig}; "
                   f"With orphan = {dst_free_orphan}")
            errors.append(msg)

        # 8. Stop the servers to enable the checker.
        dmg_command.system_stop()

        # 9. Enable and start the checker.
        dmg_command.check_enable()
        dmg_command.check_start()

        # 10. Query the checker and verify that the issue was fixed.
        # i.e., Current status is COMPLETED.
        query_msg = ""
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                query_msg = check_query_out["response"]["reports"][0]["msg"]
                break
            time.sleep(5)
        if "orphan rank" not in query_msg:
            errors.append(
                "Checker didn't fix orphan pool shard! msg = {}".format(query_msg))

        # 11. Disable the checker.
        dmg_command.check_disable()

        # 12. Restart the servers so that the storage usage in the next step is updated.
        dmg_command.system_start()

        # 13. Call dmg storage query usage to verify that the pool usage is back to the
        # original value.
        rank_to_free_fixed = self.get_rank_to_free()
        self.log.info("rank_to_free_fixed = %s", rank_to_free_fixed)
        dst_free_fixed = rank_to_free_fixed[dst_rank]
        # Free space should have been recovered to the original value, but it could be
        # a little smaller. If it's smaller than 10% of the pool size, conclude that the
        # free space hasn't been recovered.
        buffer = int(self.pool.size.value * 0.1)
        if dst_free_fixed + buffer < dst_free_orig:
            msg = (f"Destination rank space was not recovered by checker! "
                   f"Original = {dst_free_orig}; With fixed = {dst_free_fixed}")
            errors.append(msg)

        report_errors(test=self, errors=errors)
