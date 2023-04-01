"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from ClusterShell.NodeSet import NodeSet
from avocado.core.exceptions import TestFail

from apricot import TestWithServers
from general_utils import report_errors, pcmd, check_file_exists


class Pass1Test(TestWithServers):
    """Test Pass 1: Pool List Consolidation

    :avocado: recursive
    """

    def test_dangling_pool(self):
        """Test dangling pool.

        1. Create a pool.
        2. Remove the pool from the pool service (PS) on engine by calling:
        dmg faults pool-svc <pool> CIC_POOL_NONEXIST_ON_ENGINE
        3. Show dangling pool entry by calling:
        dmg pool list --no-query
        4. Enable and start the checker.
        5. Query the checker and verify the message regarding dangling pool.
        6. Disable the checker.
        7. Verify that the dangling pool was removed. Call dmg pool list and it should
        return an empty list.

        Jira ID: DAOS-11711. For test.

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=recovery,pass_1
        :avocado: tags=Pass1Test,test_dangling_pool
        """
        # 1. Create a pool.
        self.pool = self.get_pool(connect=False)

        # 2. Remove the pool from the pool service (PS) on engine.
        dmg_command = self.get_dmg_command()
        dmg_command.faults_pool_svc(
            pool=self.pool.identifier, checker_report_class="CIC_POOL_NONEXIST_ON_ENGINE")

        # 3. Show dangling pool entry.
        pools = dmg_command.get_pool_list_labels(no_query=True)
        if self.pool.identifier not in pools:
            self.fail("Dangling pool not found after removing pool from PS!")

        # 4. Enable and start the checker.
        dmg_command.check_enable()
        # Calling dmg check start will automatically fix the issue with the default
        # option, which is to remove the dangling pool in this failure type.
        dmg_command.check_start()

        # 5. Query the checker.
        query_msg = ""
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                query_msg = check_query_out["response"]["reports"][0]["msg"]
                break
            time.sleep(5)

        # Verify that the checker detected dangling pool.
        errors = []
        if "dangling pool" not in query_msg:
            errors.append(
                "Checker didn't detect dangling pool! msg = {}".format(query_msg))

        # 6. Call dmg check disable.
        dmg_command.check_disable()

        # 7. Verify that the dangling pool was removed.
        pools = dmg_command.get_pool_list_labels()
        if pools:
            errors.append(f"Dangling pool was not removed! {pools}")

        # Don't try to destroy the pool during tearDown.
        self.pool.skip_cleanup()

        report_errors(test=self, errors=errors)

    def run_checker_on_orphan_pool(self, policies=None):
        """Run step 1 to 6 of the orphan pool tests.

        1. Create a pool.
        2. Remove the PS entry on management service (MS) by calling:
        dmg faults mgmt-svc pool <pool> CIC_POOL_NONEXIST_ON_MS
        3. At this point, MS doesn't recognize any pool, but it exists on engine (orphan
        pool). Call dmg pool list and verify that it doesn't return any pool.
        4. Enable and start the checker. Use policies for trust MS test.
        5. Query the checker and verify the message regarding orphan pool.
        6. Disable the checker.

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

        # 4. Enable and start the checker.
        dmg_command.check_enable()
        # Calling dmg check start will automatically fix the issue with the default
        # option, which is to recreate the MS pool entry. We can specify policy to let the
        # checker trust MS and remove the pool.
        dmg_command.check_start(policies=policies)

        # 5. Query the checker.
        query_msg = ""
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                query_msg = check_query_out["response"]["reports"][0]["msg"]
                break
            time.sleep(5)

        # Verify that the checker detected orphan pool.
        errors = []
        if "orphan pool" not in query_msg:
            errors.append(
                "Checker didn't detect orphan pool! msg = {}".format(query_msg))

        # 6. Call dmg check disable.
        dmg_command.check_disable()

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

    def test_orphan_pool_trus_ps(self):
        """Test orphan pool with trust PS (default option).

        1. Create a pool.
        2. Remove the PS entry on management service (MS) by calling:
        dmg faults mgmt-svc pool <pool> CIC_POOL_NONEXIST_ON_MS
        3. At this point, MS doesn't recognize any pool, but it exists on engine (orphan
        pool). Call dmg pool list and verify that it doesn't return any pool.
        4. Enable and start the checker.
        5. Query the checker and verify the message regarding orphan pool.
        6. Disable the checker.
        7. Verify that the orphan pool was reconstructed. Call dmg pool list and it
        should return the pool created at step 1.

        Jira ID: DAOS-11712

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=recovery,pass_1
        :avocado: tags=Pass1Test,test_orphan_pool_trust_ps
        """
        errors = []
        # Run step 1 to 6.
        errors = self.run_checker_on_orphan_pool()

        # 7. Verify that the orphan pool was reconstructed.
        dmg_command = self.get_dmg_command()
        pools = dmg_command.get_pool_list_labels()
        if self.pool.identifier not in pools:
            errors.append(f"Orphan pool was not reconstructed! Pools = {pools}")

        report_errors(test=self, errors=errors)

    def test_orphan_pool_trus_ms(self):
        """Test orphan pool with trust MS.

        1. Create a pool.
        2. Remove the PS entry on management service (MS) by calling:
        dmg faults mgmt-svc pool <pool> CIC_POOL_NONEXIST_ON_MS
        3. At this point, MS doesn't recognize any pool, but it exists on engine (orphan
        pool). Call dmg pool list and verify that it doesn't return any pool.
        4. Enable and start the checker with POOL_NONEXIST_ON_MS:CIA_TRUST_MS.
        5. Query the checker and verify the message regarding orphan pool.
        6. Disable the checker.
        7. Verify that the orphan pool was removed. Call dmg pool list and it should
        return empty.
        8. Verify that the pool directory is removed from the mount point.

        Jira ID: DAOS-11712

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=recovery,pass_1
        :avocado: tags=Pass1Test,test_orphan_pool_trust_ms
        """
        errors = []
        # Run step 1 to 6 with the policies to trust MS during dmg check start.
        errors = self.run_checker_on_orphan_pool(
            policies="POOL_NONEXIST_ON_MS:CIA_TRUST_MS")

        # 7. Verify that the orphan pool was removed.
        dmg_command = self.get_dmg_command()
        pools = dmg_command.get_pool_list_labels()
        if pools:
            errors.append(f"Orphan pool was not removed! Pools = {pools}")

        # 8. Verify that the pool directory is removed from the mount point.
        errors = self.verify_pool_dir_removed(errors=errors)

        # Don't try to destroy the pool during tearDown.
        self.pool.skip_cleanup()

        report_errors(test=self, errors=errors)

    def test_lost_majority_ps_replicas(self):
        """Test lost the majority of PS replicas.

        1. Create a pool with --nsvc=3. Rank 0, 1, and 2 will be service replicas.
        2. Stop servers.
        3. Remove /mnt/daos/<pool_uuid>/rdb-pool from rank 0 and 2.
        4. Start servers.
        5. Enable and start the checker. It should retrieve the rdb-pool on any of the two
        ranks (except rank 1, which already has it).
        6. Query the checker and verify the message
        7. Disable the checker.
        8. Try creating a container. The pool can be started, so it should succeed.
        9. Show that rdb-pool are recovered. i.e., at least three out of four ranks
        should have rdb-pool.

        Jira ID: DAOS-12029

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=recovery,pass_1
        :avocado: tags=Pass1Test,test_lost_majority_ps_replicas
        """
        # 1. Create a pool with --nsvc=3.
        self.pool = self.get_pool(svcn=3)

        # 2. Stop servers.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 3. Remove /mnt/daos/<pool_uuid>/rdb-pool from rank 0 and 2.
        rank_to_host = self.server_managers[0].ranks
        host_0 = rank_to_host[0]
        host_2 = rank_to_host[2]
        hosts = NodeSet.fromlist([host_0, host_2])
        command = f"sudo rm /mnt/daos0/{self.pool.uuid.lower()}/rdb-pool"
        pcmd(hosts=hosts, command=command)

        # 4. Start servers.
        dmg_command.system_start()

        # 5. Enable and start the checker.
        dmg_command.check_enable()
        # Calling dmg check start will automatically fix the issue with the default
        # option, which is to retrieve the rdb-pool on any of the two ranks.
        dmg_command.check_start()

        # 6. Query the checker and verify the message
        query_msg = ""
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                query_msg = check_query_out["response"]["reports"][0]["msg"]
                break
            time.sleep(5)

        errors = []
        if "corrupted pool without quorum" not in query_msg:
            errors.append(
                "Checker didn't detect orphan pool! msg = {}".format(query_msg))

        # 7. Disable the checker.
        dmg_command.check_disable()

        # 8. Try creating a container. It should succeed.
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

        # 9. Show that rdb-pool are recovered. i.e., at least three out of four ranks
        # should have rdb-pool.
        hosts = list(set(self.server_managers[0].ranks.values()))
        count = 0
        rdb_pool_path = f"/mnt/daos0/{self.pool.uuid.lower()}/rdb-pool"
        for host in hosts:
            node = NodeSet(host)
            if check_file_exists(hosts=node, filename=rdb_pool_path):
                count += 1
                self.log.info("rdb-pool found at %s", str(node))

        self.log.info("rdb-pool count = %d", count)
        if count < len(hosts) - 1:
            errors.append(f"Not enough rdb-pool has been recovered! - {count} ranks")

        report_errors(test=self, errors=errors)

    def test_lost_all_rdb(self):
        """Remove rdb-pool from all mount point from all nodes. Now the pool can’t be
        recovered, so checker should remove it from both MS and engine.

        1. Create a pool.
        2. Stop servers.
        3. Remove /mnt/daos0/<pool_uuid>/rdb-pool from all ranks.
        4. Start servers.
        5. Enable and start the checker. It should remove pool from MS and engine.
        6. Query the checker and verify the message.
        7. Disable the checker.
        8. Check that the pool does not appear with dmg pool list.
        9. Verify that the pool directory was removed from the mount point.

        Jira ID: DAOS-12067

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=recovery,pass_1
        :avocado: tags=Pass1Test,test_lost_all_rdb
        """
        # 1. Create a pool.
        self.pool = self.get_pool()

        # 2. Stop servers.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 3. Remove /mnt/daos/<pool_uuid>/rdb-pool from all ranks.
        hosts = list(set(self.server_managers[0].ranks.values()))
        nodeset_hosts = NodeSet.fromlist(hosts)
        command = f"sudo rm /mnt/daos0/{self.pool.uuid.lower()}/rdb-pool"
        remove_result = pcmd(hosts=nodeset_hosts, command=command)
        success_nodes = remove_result[0]
        if nodeset_hosts != success_nodes:
            msg = (f"Failed to remove rdb-pool! All = {nodeset_hosts}, "
                   f"Success = {success_nodes}")
            self.fail(msg)

        # 4. Start servers.
        dmg_command.system_start()

        # 5. Enable and start the checker.
        dmg_command.check_enable()
        # Calling dmg check start will automatically fix the issue with the default
        # option, which is to discard the pool.
        dmg_command.check_start()

        # 6. Query the checker and verify the message.
        query_msg = ""
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                if check_query_out["response"]["reports"]:
                    query_msg = check_query_out["response"]["reports"][0]["msg"]
                    break
            time.sleep(5)

        errors = []
        if "corrupted pool without quorum" not in query_msg:
            errors.append(
                "Checker didn't detect orphan pool! msg = {}".format(query_msg))

        # 7. Disable the checker.
        dmg_command.check_disable()

        # 8. Check that the pool does not appear with dmg pool list.
        pools = dmg_command.get_pool_list_all()
        if pools:
            errors.append(f"Pool still exists after running checker! {pools}")

        # 9. Verify that the pool directory was removed from the mount point.
        errors = self.verify_pool_dir_removed(errors=errors)

        # Don't try to destroy the pool during tearDown.
        self.pool.skip_cleanup()

        report_errors(test=self, errors=errors)
