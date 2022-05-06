#!/usr/bin/python
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import os
import threading

from ior_test_base import IorTestBase
from ior_utils import IorCommand
from general_utils import report_errors, stop_processes
from command_utils_base import CommandFailure
from job_manager_utils import get_job_manager


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
            timeout (int): Mpirun timeout value in sec. Defaults to None, in which case
                infinite.
        """
        # Update the object class depending on the test case.
        ior_cmd = IorCommand(namespace=namespace)
        ior_cmd.get_params(self)

        # Standard IOR prep sequence.
        ior_cmd.set_daos_params(self.server_group, pool, container.uuid)
        testfile = os.path.join("/", file_name)
        ior_cmd.test_file.update(testfile)

        manager = get_job_manager(
            test=self, class_name="Mpirun", job=ior_cmd, subprocess=self.subprocess,
            mpi_type="mpich", timeout=timeout)
        manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)

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

    @staticmethod
    def check_container_health(container, expected_health):
        """Check container property's Health field by calling daos container get-prop.

        Args:
            container (TestContainer): Container to call get-prop.
            expected_health (str): Expected value in the Health field.

        Returns:
            bool: True if expected_health matches the one obtained from get-prop.

        """
        output = container.get_prop(properties=["status"])
        actual_health = output["response"][0]["value"]

        return actual_health == expected_health

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
        result = stop_processes(hosts=[engine_kill_host], pattern=pattern)
        if 0 in result and len(result) == 1:
            self.log.info("No remote daos_engine process killed!")
        else:
            self.log.info("daos_engine in %s killed", engine_kill_host)

    def verify_ior_worked(self, ior_results, job_num, errors):
        """Verify that the IOR worked.

        Args:
            ior_results (dict): Dictionary that contains the IOR run results.
            job_num (int): Job number used for the IOR run.
            errors (list): Error list used in the test.
        """
        self.log.info(ior_results)
        if not ior_results[job_num][0]:
            ior_error = ior_results[job_num][1]
            errors.append("Error found in second IOR run! {}".format(ior_error))

    def verify_rank_failure(self, ior_namespace, container_namespace):
        """Verify engine failure can be recovered by restarting daos_server.

        1. Create a pool and a container. Create a container with or without redundancy
        factor based on container_namespace.
        2. Run IOR with given object class.
        3. While IOR is running, kill daos_engine processes from one of the nodes to
        simulate a node failure. (Two engines per node.)
        4. Verify that IOR failed.
        5. Restart daos_server service on the two nodes.
        6. Verify the system status by calling dmg system query.
        7. Verify that the container Health is HEALTHY.
        8. Run IOR to the same container and verify that it works.

        Args:
            ior_namespace (str): Yaml namespace that defines the object class used for
                IOR.
            container_namespace (str): Yaml namespace that defines the container
                redundancy factor.
        """
        # 1. Create a pool and a container.
        self.add_pool(namespace="/run/pool_size_ratio_80/*")
        self.add_container(pool=self.pool, namespace=container_namespace)

        # 2. Run IOR.
        ior_results = {}
        job_num = 1
        # If we don't use timeout, when the engines are killed, Mpirun gets stuck and
        # waits forever. If we use timeout that's too long, the daos container command
        # after the server restart will get stuck, so use 10 sec timeout, which is the
        # same as deadlineForStonewalling in IOR.
        ior_thread = threading.Thread(
            target=self.run_ior_report_error,
            args=[ior_results, job_num, "test_file_1", self.pool, self.container,
                  ior_namespace, 10])

        ior_thread.start()

        # Wait for a few seconds for IOR to start.
        self.log.info("Waiting 5 sec for IOR to start writing data...")
        time.sleep(5)

        # 3. While IOR is running, kill daos_engine from two of the ranks.
        # Arbitrarily select node index, say index 1.
        self.kill_engine(engine_kill_host=self.hostlist_servers[1])

        # Wait for IOR to complete.
        ior_thread.join()

        # 4. Verify that IOR failed.
        errors = []
        self.log.info("----- IOR results 1 -----")
        self.log.info(ior_results)
        if job_num not in ior_results or ior_results[job_num][0]:
            errors.append("First IOR didn't fail as expected!")

        # 5. Restart daos_servers. It's not easy to restart only on the host where the
        # engines were killed, so just restart all.
        self.restart_all_servers()

        # 6. Verify the system status by calling dmg system query.
        output = self.get_dmg_command().system_query()
        for member in output["response"]["members"]:
            if member["state"] != "joined":
                errors.append("Server rank {} state isn't joined!".format(member["rank"]))

        # 7. Verify that the container Health is HEALTHY.
        if not self.check_container_health(
                container=self.container, expected_health="HEALTHY"):
            errors.append("Container health isn't HEALTHY after server restart!")

        # 8. Run IOR and verify that it works.
        ior_results = {}
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_2",
            pool=self.pool, container=self.container, namespace=ior_namespace)

        self.log.info("----- IOR results 2 -----")
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")

    def test_rank_failure_wo_rf(self):
        """Jira ID: DAOS-10002.

        Test rank failure without redundancy factor and SX object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,rank_failure
        :avocado: tags=rank_failure_wo_rf
        """
        self.verify_rank_failure(
            ior_namespace="/run/ior_wo_rf/*",
            container_namespace="/run/container_wo_rf/*")

    def test_rank_failure_with_rp(self):
        """Jira ID: DAOS-10002.

        Test rank failure with redundancy factor and RP_2G1 object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,rank_failure
        :avocado: tags=rank_failure_with_rp
        """
        self.verify_rank_failure(
            ior_namespace="/run/ior_with_rp/*",
            container_namespace="/run/container_with_rf/*")

    def test_rank_failure_with_ec(self):
        """Jira ID: DAOS-10002.

        Test rank failure with redundancy factor and EC_2P1G1 object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,rank_failure
        :avocado: tags=rank_failure_with_ec
        """
        self.verify_rank_failure(
            ior_namespace="/run/ior_with_ec/*",
            container_namespace="/run/container_with_rf/*")

    def test_rank_failure_isolation(self):
        """Jira ID: DAOS-10002.

        Stop daos_engine where pool is not created.

        1. Determine the two ranks to create the pool and a node to kill the engines.
        2. Create a pool across two ranks on the same node.
        3. Create a container without redundancy factor.
        4. Run IOR with oclass SX.
        5. While IOR is running, kill daos_engine process from two of the ranks where the
        pool isn’t created. This will simulate the case where there’s a node failure, but
        doesn’t affect the user because their pool isn’t created on the failed node
        (assuming that everything else such as network, client node, etc. are still
        working).
        6. Verify that IOR finishes successfully.
        7. Verify that the container Health is HEALTHY.
        8. To further verify that the pool isn’t affected, create a new container on the
        pool and run IOR.
        9. To make avocado happy, restart daos_servers on the host where the engines were
        killed.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,rank_failure
        :avocado: tags=rank_failure_isolation
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
        self.add_pool(namespace="/run/pool_size_value/*", create=False)
        self.pool.target_list.update([0, rank_r])
        self.pool.create()

        # 3. Create a container without redundancy factor.
        self.container = []
        self.container.append(
            self.get_container(pool=self.pool, namespace="/run/container_wo_rf/*"))

        # 3. Run IOR with oclass SX.
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

        # 4. Kill daos_engine from the host where the pool isn't created.
        self.kill_engine(engine_kill_host=engine_kill_host)

        # Wait for IOR to complete.
        ior_thread.join()

        # 5. Verify that IOR worked.
        errors = []
        self.log.info("----- IOR results 1 -----")
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        # 6. Verify that the container Health is HEALTHY.
        if not self.check_container_health(
                container=self.container[0], expected_health="HEALTHY"):
            errors.append(
                "Container health isn't HEALTHY after killing engine on rank 1!")

        # 7. Create a new container on the pool and run IOR.
        self.container.append(
            self.get_container(pool=self.pool, namespace="/run/container_wo_rf/*"))

        # Run IOR and verify that it works.
        ior_results = {}
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
