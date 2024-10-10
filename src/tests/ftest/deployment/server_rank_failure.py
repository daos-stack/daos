"""
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import threading
import time

from ClusterShell.NodeSet import NodeSet
from command_utils_base import CommandFailure
from general_utils import report_errors
from ior_test_base import IorTestBase
from ior_utils import IorCommand
from job_manager_utils import get_job_manager
from run_utils import stop_processes


class ServerRankFailure(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify server rank, or engine, failure is properly handled
    and recovered.

    :avocado: recursive
    """
    def run_ior_report_error(self, results, job_num, file_name, pool, container,
                             namespace, timeout=None):
        """Run IOR command and store the results to results dictionary.

        Create a new IorCommand object instead of using the one in IorTestBase because
        we'll run a test that runs multiple IOR processes at the same time.

        Args:
            results (dict): A dictionary object to store the ior metrics
            job_num (int): Assigned job number
            file_name (str): File name used for self.ior_cmd.test_file.
            pool (TestPool): Pool to run IOR.
            container (TestContainer): Container to run IOR.
            namespace (str): IOR namespace.
            timeout (int, optional): Mpirun timeout value in sec.
                Defaults to None, in which case infinite.
        """
        # Update the object class depending on the test case.
        ior_cmd = IorCommand(self.test_env.log_dir, namespace=namespace)
        ior_cmd.get_params(self)

        # Standard IOR prep sequence.
        ior_cmd.set_daos_params(pool, container.identifier)
        ior_cmd.update_params(test_file=os.path.join(os.sep, file_name))

        manager = get_job_manager(
            test=self, job=ior_cmd, subprocess=self.subprocess, timeout=timeout)
        manager.assign_hosts(self.hostlist_clients, self.workdir, self.hostfile_clients_slots)
        ppn = self.params.get("ppn", namespace)
        manager.update_params(ppn=ppn, processes=None)

        # Run the command.
        try:
            self.log.info("--- IOR command %d start ---", job_num)
            ior_output = manager.run()
            results[job_num] = [True]
            # For debugging.
            results[job_num].extend(IorCommand.get_ior_metrics(ior_output))
            # Command worked, but append the error message if any.
            results[job_num].append(ior_output.stderr_text)
            self.log.info("--- IOR command %d end ---", job_num)
        except CommandFailure as error:
            self.log.info("--- IOR command %d failed ---", job_num)
            results[job_num] = [False, "IOR failed: {}".format(error)]

    def restart_all_servers(self):
        """Restart daos_server on all nodes.

        Stop on all nodes; both healthy nodes and the node where engines were killed.
        Then start on all nodes.
        """
        self.log.info("Call systemctl stop daos_server")
        # self.server_managers[0].stop() is the usual way, but this will also stop
        # reset storage etc., which will make the server not start in the next step. It's
        # also not the representation of the expected user behavior. Here, we just want
        # to call systemctl stop daos_server.
        # Calling manager.stop() is the equivalent of super().stop() in
        # DaosServerManager.stop().
        self.server_managers[0].manager.stop()
        self.log.info("Start daos_server and detect the DAOS I/O engine message")
        self.server_managers[0].restart(hosts=self.hostlist_servers)

    def kill_engine(self, engine_kill_host):
        """Kill engine on the given host.

        Args:
            engine_kill_host (str): Hostname to kill engine.
        """
        pattern = self.server_managers[0].manager.job.command_regex
        detected, running = stop_processes(self.log, NodeSet(engine_kill_host), pattern)
        if not detected:
            self.log.info("No daos_engine process killed on %s!", engine_kill_host)
        elif running:
            self.log.info("Unable to kill daos_engine processes on %s!", running)
        else:
            self.log.info("daos_engine processes on %s killed", detected)

    def verify_ior_worked(self, ior_results, job_num, errors):
        """Verify that the IOR worked.

        Args:
            ior_results (dict): Dictionary that contains the IOR run results.
            job_num (int): Job number used for the IOR run.
            errors (list): Error list used in the test.
        """
        self.log.info(ior_results[job_num])
        if not ior_results[job_num][0]:
            ior_error = ior_results[job_num][1]
            errors.append("Error found in IOR job {}! {}".format(job_num, ior_error))

    def verify_rank_failure(self, ior_namespace):
        """Verify engine failure can be recovered by restarting daos_server.

        1. Create a pool and a container. Create a container with redundancy factor.
        2. Run IOR with given object class and let it run through step 7.
        3. While IOR is running, kill all daos_engine on a non-access-point node
        4. Wait for IOR to complete.
        5. Verify that IOR failed.
        6. Wait for rebuild to finish
        7. Restart daos_servers.
        8. Verify the system status by calling dmg system query.
        9. Call dmg pool query -b to find the disabled ranks.
        10. Call dmg pool reintegrate one rank at a time to enable all ranks.
        11. Verify that the container Health is HEALTHY.
        12. Run IOR and verify that it works.

        Args:
            ior_namespace (str): Yaml namespace that defines the object class used for IOR.
        """
        # 1. Create a pool and a container.
        self.add_pool(namespace="/run/pool_size_ratio_80/*")
        self.add_container(pool=self.pool)

        # 2. Run IOR with given object class and let it run through step 7.
        ior_results = {}
        errors = []
        job_num = 1
        mpirun_timeout = self.params.get('sw_deadline', ior_namespace)
        self.log.info("Running Mpirun-IOR with Mpirun timeout of %s sec", mpirun_timeout)
        ior_thread = threading.Thread(
            target=self.run_ior_report_error,
            args=[ior_results, job_num, "test_file_1", self.pool, self.container,
                  ior_namespace, mpirun_timeout])

        ior_thread.start()

        # Wait for a few seconds for IOR to start.
        self.log.info("Waiting 5 sec for IOR to start writing data...")
        time.sleep(5)

        # 3. While IOR is running, kill all daos_engine on a non-access-point node
        engine_kill_host = self.hostlist_servers[1]
        self.kill_engine(engine_kill_host=engine_kill_host)

        # 4. Wait for IOR to complete.
        ior_thread.join()

        # 5. Verify that IOR failed.
        self.log.info("----- IOR results %d -----", job_num)
        self.log.info(ior_results[job_num])
        if job_num not in ior_results or ior_results[job_num][0]:
            errors.append("First IOR didn't fail as expected!")

        # 6. Wait for rebuild to finish.
        self.log.info("Wait for rebuild to start.")
        self.pool.wait_for_rebuild_to_start(interval=10)
        self.log.info("Wait for rebuild to finish.")
        self.pool.wait_for_rebuild_to_end(interval=10)

        # 7. Restart daos_servers. It's not easy to restart only on the host where the
        # engines were killed, so just restart all.
        self.restart_all_servers()

        # 8. Verify the system status by calling dmg system query.
        output = self.get_dmg_command().system_query()
        for member in output["response"]["members"]:
            if member["state"] != "joined":
                errors.append("Server rank {} state isn't joined!".format(member["rank"]))

        # 9. Call dmg pool query -b to find the disabled ranks.
        output = self.get_dmg_command().pool_query(pool=self.pool.identifier)
        disabled_ranks = output["response"].get("disabled_ranks")
        self.log.info("Disabled ranks = %s", disabled_ranks)

        # 10. Call dmg pool reintegrate one rank at a time to enable all ranks.
        for disabled_rank in disabled_ranks:
            while True:
                try:
                    self.pool.reintegrate(rank=disabled_rank)
                    break
                except CommandFailure as error:
                    self.log.debug("## pool reintegrate error: %s", error)

            # Wait for rebuild to finish
            self.log.info("Wait for rebuild to start.")
            self.pool.wait_for_rebuild_to_start(interval=10)
            self.log.info("Wait for rebuild to finish.")
            self.pool.wait_for_rebuild_to_end(interval=10)

        # 11. Verify that the container Health is HEALTHY.
        if not self.container.verify_prop({"status": "HEALTHY"}):
            errors.append("Container health isn't HEALTHY after server restart!")

        # 12. Run IOR and verify that it works.
        job_num = 2
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_2",
            pool=self.pool, container=self.container, namespace=ior_namespace)

        self.log.info("----- IOR results %d -----", job_num)
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")

    def test_server_rank_failure_with_rp(self):
        """Jira ID: DAOS-10002.

        Test rank failure with redundancy factor and RP_3GX object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,server_rank_failure,rebuild
        :avocado: tags=ServerRankFailure,test_server_rank_failure_with_rp
        """
        self.verify_rank_failure(ior_namespace="/run/ior_with_rp/*")

    def test_server_rank_failure_with_ec(self):
        """Jira ID: DAOS-10002.

        Test rank failure with redundancy factor and EC_4P2GX object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,server_rank_failure,rebuild
        :avocado: tags=ServerRankFailure,test_server_rank_failure_with_ec
        """
        self.verify_rank_failure(ior_namespace="/run/ior_with_ec/*")

    def test_server_rank_failure_isolation(self):
        """Jira ID: DAOS-10002.

        Stop daos_engine where pool is not created.

        1. Determine the two ranks to create the pool and a node to kill the engines.
        2. Create a pool across two ranks on the same node.
        3. Create a container without redundancy factor.
        4. Run IOR with oclass SX.
        5. While IOR is running, kill daos_engine process from two of the ranks where the
        pool isn't created. This will simulate the case where there's a node failure, but
        does not affect the user because their pool isn't created on the failed node
        (assuming that everything else such as network, client node, etc. are still
        working).
        6. Verify that IOR finishes successfully.
        7. Verify that the container Health is HEALTHY.
        8. To further verify that the pool isn't affected, create a new container on the
        pool and run IOR.
        9. To make avocado happy, restart daos_servers on the host where the engines were
        killed.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,server_rank_failure
        :avocado: tags=ServerRankFailure,test_server_rank_failure_isolation
        """
        # 1. Determine the two ranks to create the pool and a node to kill the engines.
        # We'll create a pool on rank 0 and the other rank that's on the same node. Find
        # hostname of rank 0.
        rank_0_host = None
        output = self.get_dmg_command().system_query()
        members = output["response"]["members"]
        for member in members:
            if member["rank"] == 0:
                rank_0_host = member["addr"].split(":")[0]
                break
        self.log.info("rank 0 host = %s", rank_0_host)

        # Find the other rank that's on the same node as in rank 0. Call it rank_r.
        rank_r = None
        for member in members:
            if member["addr"].split(":")[0] == rank_0_host and member["rank"] != 0:
                rank_r = member["rank"]
                break
        self.log.info("rank_r = %s", rank_r)

        # Find the hostname that's different from rank_0_host. We'll kill engines on it.
        engine_kill_host = None
        for member in members:
            host = member["addr"].split(":")[0]
            if rank_0_host != host:
                engine_kill_host = host
                break
        self.log.info("engine_kill_host = %s", engine_kill_host)

        # 2. Create a pool across two ranks on the same node; 0 and rank_r.
        self.add_pool(namespace="/run/pool_size_value/*", target_list=[0, rank_r])

        # 3. Create a container without redundancy factor.
        self.container = []
        self.container.append(
            self.get_container(pool=self.pool, namespace="/run/container_wo_rf/*"))

        # 4. Run IOR with oclass SX.
        ior_results = {}
        job_num = 1
        ior_namespace = "/run/ior_wo_rf/*"
        ior_thread = threading.Thread(
            target=self.run_ior_report_error,
            args=[ior_results, job_num, "test_file_1", self.pool, self.container[0],
                  ior_namespace])

        ior_thread.start()

        # Wait for a few seconds for IOR to start.
        self.log.info("Waiting 5 sec for IOR to start writing data...")
        time.sleep(5)

        # 5. Kill daos_engine from the host where the pool isn't created.
        self.kill_engine(engine_kill_host=engine_kill_host)

        # Wait for IOR to complete.
        ior_thread.join()

        # 6. Verify that IOR worked.
        errors = []
        self.log.info("----- IOR results 1 -----")
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        # 7. Verify that the container Health is HEALTHY.
        if not self.container[0].verify_prop({"status": "HEALTHY"}):
            errors.append("Container health isn't HEALTHY after killing engine on rank 1!")

        # 8. Create a new container on the pool and run IOR.
        self.container.append(
            self.get_container(pool=self.pool, namespace="/run/container_wo_rf/*"))

        # Run IOR and verify that it works.
        job_num = 2
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_2",
            pool=self.pool, container=self.container[1], namespace=ior_namespace)

        self.log.info("----- IOR results 2 -----")
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        # 9. To make avocado happy, restart daos_servers.
        self.restart_all_servers()

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")
