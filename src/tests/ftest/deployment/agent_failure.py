"""
  (C) Copyright 2022-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import threading
import time

from ClusterShell.NodeSet import NodeSet
from command_utils_base import CommandFailure
from general_utils import get_journalctl, journalctl_time, report_errors
from ior_test_base import IorTestBase
from ior_utils import IorCommand
from job_manager_utils import get_job_manager
from run_utils import stop_processes


class AgentFailure(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify agent failure is properly handled.

    :avocado: recursive
    """
    def run_ior_collect_error(self, results, job_num, file_name, clients, namespace):
        """Run IOR command and store error in results.

        Args:
            results (dict): A dictionary object to store the ior metrics.
            job_num (int): Assigned job number.
            file_name (str): File name used for self.ior_cmd.test_file.
            clients (list): Client hostnames to run IOR from.
            namespace (str): IOR namespace.
        """
        ior_cmd = IorCommand(self.test_env.log_dir, namespace=namespace)
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(self.pool, self.container.identifier)
        testfile = os.path.join(os.sep, file_name)
        ior_cmd.update_params(test_file=testfile)

        # We need to provide hostnames to the util files with NodeSet.
        clients_nodeset = NodeSet.fromlist(clients)

        manager = get_job_manager(
            test=self, class_name="Mpirun", job=ior_cmd, subprocess=self.subprocess)
        manager.assign_hosts(clients_nodeset, self.workdir, self.hostfile_clients_slots)
        ppn = self.params.get("ppn", '/run/ior/client_processes/*')
        manager.assign_processes(ppn=ppn)

        try:
            ior_output = manager.run()
            results[job_num] = [True]
            # For debugging.
            results[job_num].extend(IorCommand.get_ior_metrics(ior_output))
            # We'll verify the error message.
            results[job_num].append(ior_output.stderr_text)
        except CommandFailure as error:
            results[job_num] = [False, "IOR failed: {}".format(error)]

    def test_agent_failure(self):
        """Jira ID: DAOS-9385.

        1. Create a pool and a container.
        2. Run IOR.
        3. Stop daos_agent process while IOR is running.
        4. Check the error on the client side. When daos_agent is killed in the middle of
        IOR, the IOR would fail.
        5. Verify journalctl shows the log that the agent is stopped. Call:
        journalctl --system -t daos_agent --since <before> --until <after>
        This step verifies that DAOS, or daos_agent process in this case, prints useful
        logs for the user to troubleshoot the issue, which in this case the application
        can't be used.
        6. Restart daos_agent.
        7. Run IOR again. It should succeed this time without any error. This step
        verifies that DAOS can recover from the fault with minimal human intervention.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,fault_management,agent_failure
        :avocado: tags=AgentFailure,test_agent_failure
        """
        # 1. Create a pool and a container.
        self.add_pool()
        self.add_container(self.pool)

        # 2. Run IOR.
        ior_results = {}
        job_num = 1
        ior_namespace = "/run/ior_with_ec/*"
        self.log.info("Run IOR with thread")
        job = threading.Thread(
            target=self.run_ior_collect_error,
            args=[ior_results, job_num, "test_file_1", [self.hostlist_clients[0]], ior_namespace])

        self.log.info("Start IOR %d (thread)", job_num)
        job.start()

        # We need to stop daos_agent while IOR is running, so need to wait for a few
        # seconds for IOR to start.
        self.log.info("Waiting 10 sec for IOR to start writing data...")
        time.sleep(10)

        errors = []

        # 3. Stop daos_agent process while IOR is running.
        since = journalctl_time()
        self.log.info("Stopping agent")
        stop_agent_errors = self.stop_agents()
        for error in stop_agent_errors:
            self.log.debug(error)
            errors.append(error)
        until = journalctl_time()

        # Wait until the IOR thread ends.
        job.join()

        # 4. Verify the error from the IOR command.
        self.log.info("--- IOR results %d ---", job_num)
        self.log.info(ior_results[job_num])
        if ior_results[job_num][0]:
            errors.append("IOR worked when agent is killed!")

        # 5. Verify journalctl shows the log that the agent is stopped.
        results = get_journalctl(
            hosts=self.hostlist_clients, since=since, until=until,
            journalctl_type="daos_agent")
        self.log.info("journalctl results = %s", results)
        if "shutting down" not in results[0]["data"]:
            msg = "Agent shut down message not found in journalctl! Output = {}".format(
                results)
            errors.append(msg)

        # 6. Restart agent.
        self.log.info("Restart agent")
        self.start_agent_managers()

        # 7. Run IOR again.
        job_num = 2
        self.log.info("Start IOR %d", job_num)
        self.run_ior_collect_error(
            job_num=job_num, results=ior_results, file_name="test_file_2",
            clients=[self.hostlist_clients[0]], namespace=ior_namespace)

        # Verify that there's no error this time.
        self.log.info("--- IOR results %d ---", job_num)
        self.log.info(ior_results[job_num])
        if not ior_results[job_num][0]:
            ior_error = ior_results[job_num][-1]
            errors.append("IOR with restarted agent failed! Error: {}".format(ior_error))

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")

    def test_agent_failure_isolation(self):
        """Jira ID: DAOS-9385.

        1. Create a pool and a container.
        2. Run IOR from the two client nodes.
        3. Stop daos_agent process while IOR is running on one of the clients.
        4. Wait until both of the IOR ends.
        5. Check that there's error on the kill client, but not on the keep client.
        6. On the killed client, verify journalctl shows the log that the agent is
        stopped.
        7. On the other client where agent is still running, verify that the journalctl
        doesn't show that the agent is stopped.
        8. Restart both daos_agent.
        9. Run IOR again from the keep client. It should succeed without any error.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,fault_management,agent_failure
        :avocado: tags=AgentFailure,test_agent_failure_isolation
        """
        # 1. Create a pool and a container.
        self.add_pool()
        self.add_container(self.pool)

        # Use the last two agent hosts, since the first is likely the test runner node
        agent_hosts = self.agent_managers[0].hosts
        self.log.info("agent_hosts = %s", agent_hosts)
        if len(agent_hosts) < 2:
            self.fail("Need at least two agent hosts!")
        agent_host_keep, agent_host_kill = agent_hosts[-2:]

        # 2. Run IOR from the two client nodes.
        ior_results = {}
        job_num_keep = 1
        job_num_kill = 2
        self.log.info("Run IOR with thread")
        ior_namespace = "/run/ior_wo_rf/*"
        thread_1 = threading.Thread(
            target=self.run_ior_collect_error,
            args=[ior_results, job_num_keep, "test_file_1", [agent_host_keep], ior_namespace])
        thread_2 = threading.Thread(
            target=self.run_ior_collect_error,
            args=[ior_results, job_num_kill, "test_file_2", [agent_host_kill], ior_namespace])

        self.log.info("Start IOR 1 (thread)")
        thread_1.start()
        thread_2.start()

        # We need to stop daos_agent while IOR is running, so need to wait for a few
        # seconds for IOR to start.
        self.log.info("Waiting 10 sec for IOR to start writing data...")
        time.sleep(10)

        errors = []

        # 3. Stop daos_agent process while IOR is running on one of the clients.
        since = journalctl_time()
        self.log.info("Stopping agent on %s", agent_host_kill)
        pattern = self.agent_managers[0].manager.job.command_regex
        detected, running = stop_processes(self.log, hosts=agent_host_kill, pattern=pattern)
        if not detected:
            msg = "No daos_agent process killed on {}!".format(agent_host_kill)
            errors.append(msg)
        elif running:
            msg = "Unable to kill daos_agent processes on {}!".format(running)
            errors.append(msg)
        else:
            self.log.info("daos_agent processes on %s killed", detected)
        until = journalctl_time()

        # 4. Wait until both of the IOR thread ends.
        thread_1.join()
        thread_2.join()

        # 5. Check that there's error on the kill client, but not on the keep client.
        self.log.info("--- IOR results Kill ---")
        self.log.info(ior_results[job_num_kill])
        if ior_results[job_num_kill][0]:
            errors.append("IOR on agent kill host worked!")

        self.log.info("--- IOR results Keep ---")
        self.log.info(ior_results[job_num_keep])
        if not ior_results[job_num_keep][0]:
            ior_error = ior_results[job_num_keep][-1]
            errors.append("Error found in IOR on keep client! {}".format(ior_error))

        # 6. On the killed client, verify journalctl shows the log that the agent is
        # stopped.
        results = get_journalctl(
            hosts=NodeSet(agent_host_kill), since=since, until=until,
            journalctl_type="daos_agent")
        self.log.info("journalctl results (kill) = %s", results)
        if "shutting down" not in results[0]["data"]:
            msg = ("Agent shut down message not found in journalctl on killed client! "
                   "Output = {}".format(results))
            errors.append(msg)

        # 7. On the other client where agent is still running, verify that the journalctl
        # in the previous step doesn't show that the agent is stopped.
        results = get_journalctl(
            hosts=NodeSet(agent_host_keep), since=since, until=until,
            journalctl_type="daos_agent")
        self.log.info("journalctl results (keep) = %s", results)
        if "shutting down" in results[0]["data"]:
            msg = ("Agent shut down message found in journalctl on keep client! "
                   "Output = {}".format(results))
            errors.append(msg)

        # 8. Restart both daos_agent. (Currently, there's no clean way to restart one.)
        self.start_agent_managers()

        # 9. Run IOR again from the keep client. It should succeed without any error.
        self.log.info("--- Start IOR 2 ---")
        self.run_ior_collect_error(
            job_num=job_num_keep, results=ior_results, file_name="test_file_3",
            clients=agent_hosts, namespace=ior_namespace)

        # Verify that there's no error.
        self.log.info("--- IOR results 2 ---")
        self.log.info(ior_results[job_num_keep])
        if not ior_results[job_num_keep][0]:
            ior_error = ior_results[job_num_keep][-1]
            errors.append("Error found in second IOR run! {}".format(ior_error))

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")
