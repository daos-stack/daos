#!/usr/bin/python
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from datetime import datetime
import os
import threading

from ior_test_base import IorTestBase
from ior_utils import IorCommand
from general_utils import report_errors, stop_processes, get_journalctl
from command_utils_base import CommandFailure
from job_manager_utils import get_job_manager


class AgentFailureIsolate(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify one agent failure doesn't affect the other client.

    :avocado: recursive
    """
    def run_ior_collect_error(self, results, job_num, file_name, clients):
        """Run IOR command and store error in results.

        Args:
            results (dict): A dictionary object to store the ior metrics
            job_num (int): Assigned job number
            file_name (str): File name used for self.ior_cmd.test_file.
            client (list): Client hostnames to run IOR from.
        """
        ior_cmd = IorCommand()
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(
            group=self.server_group, pool=self.pool, cont_uuid=self.container.uuid)
        testfile = os.path.join("/", file_name)
        ior_cmd.test_file.update(testfile)

        manager = get_job_manager(
            test=self, class_name="Mpirun", job=ior_cmd, subprocess=self.subprocess,
            mpi_type="mpich")
        manager.assign_hosts(clients, self.workdir, self.hostfile_clients_slots)
        ppn = self.params.get("ppn", '/run/ior/client_processes/*')
        manager.ppn.update(ppn, 'mpirun.ppn')
        manager.processes.update(None, 'mpirun.np')

        try:
            ior_output = manager.run()
            results[job_num] = [True]
            # For debugging.
            results[job_num].extend(IorCommand.get_ior_metrics(ior_output))
            # We'll verify the error message.
            results[job_num].append(ior_output.stderr_text)
        except CommandFailure as error:
            results[job_num] = [False, "IOR failed: {}".format(error)]

    def test_agent_failure_isolation(self):
        """Jira ID: DAOS-9385.

        1. Create a pool and a container.
        2. Run IOR from the two client nodes.
        3. Stop daos_agent process while IOR is running on one of the clients.
        4. Wait until both of the IOR ends.
        5. Check that there's no error on both of the clients. (No error occurs if there's
        another agent running.)
        6. On the killed client, verify journalctl shows the log that the agent is
        stopped.
        7. On the other client where agent is still running, verify that the journalctl
        doesn't show that the agent is stopped.
        8. Restart both daos_agent.
        9. Run IOR again from both clients. It should succeed this time without any
        error.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=deployment,fault_management,agent_failure
        :avocado: tags=agent_failure_isolation
        """
        # 1. Create a pool and a container.
        self.add_pool()
        self.add_container(self.pool)

        agent_hosts = self.agent_managers[0].hosts
        self.log.info("agent_hosts = %s", agent_hosts)
        agent_host_keep = agent_hosts[0]
        agent_host_kill = agent_hosts[1]

        # 2. Run IOR from the two client nodes.
        ior_results = {}
        job_num_keep = 1
        job_num_kill = 2
        self.log.info("Run IOR with thread")
        thread_1 = threading.Thread(
            target=self.run_ior_collect_error,
            args=[ior_results, job_num_keep, "test_file_1", [agent_host_keep]])
        thread_2 = threading.Thread(
            target=self.run_ior_collect_error,
            args=[ior_results, job_num_kill, "test_file_2", [agent_host_kill]])

        self.log.info("Start IOR 1 (thread)")
        thread_1.start()
        thread_2.start()

        # We need to stop daos_agent while IOR is running, so need to wait for a few
        # seconds for IOR to start.
        self.log.info("Sleep 3 sec")
        time.sleep(3)

        errors = []

        # 3. Stop daos_agent process while IOR is running on one of the clients.
        since = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.log.info("Stopping agent on %s", agent_host_kill)
        pattern = self.agent_managers[0].manager.job.command_regex
        result = stop_processes(hosts=[agent_host_kill], pattern=pattern)
        if 0 in result and len(result) == 1:
            msg = "No daos_agent process killed from {}!".format(agent_host_kill)
            errors.append(msg)
        else:
            self.log.info("daos_agent in %s killed", agent_host_kill)
        until = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

        # 4. Wait until both of the IOR thread ends.
        thread_1.join()
        thread_2.join()

        # 5. Check that there's no error on both of the clients. (No error occurs if
        # there's another agent running.)
        self.log.info("--- IOR results Kill ---")
        self.log.info(ior_results[job_num_kill])
        ior_error = ior_results[job_num_kill][-1]
        if ior_error:
            errors.append("Error found in IOR on kill client! {}".format(ior_error))

        self.log.info("--- IOR results Keep ---")
        self.log.info(ior_results[job_num_keep])
        ior_error = ior_results[job_num_keep][-1]
        if ior_error:
            errors.append("Error found in IOR on keep client! {}".format(ior_error))

        # 6. On the killed client, verify journalctl shows the log that the agent is
        # stopped.
        results = get_journalctl(
            hosts=[agent_host_kill], since=since, until=until,
            journalctl_type="daos_agent")
        self.log.info("journalctl results (kill) = %s", results)
        if "shutting down" not in results[0]["data"]:
            msg = ("Agent shut down message not found in journalctl on killed client! "
                   "Output = {}".format(results))
            errors.append(msg)

        # 7. On the other client where agent is still running, verify that the journalctl
        # in the previous step doesn't show that the agent is stopped.
        results = get_journalctl(
            hosts=[agent_host_keep], since=since, until=until,
            journalctl_type="daos_agent")
        self.log.info("journalctl results (keep) = %s", results)
        if "shutting down" in results[0]["data"]:
            msg = ("Agent shut down message found in journalctl on keep client! "
                   "Output = {}".format(results))
            errors.append(msg)

        # 8. Restart both daos_agent. (Currently, there's no clean way to restart one.)
        self.start_agent_managers()

        # 9. Run IOR again from both clients. It should succeed this time without any
        # error.
        self.log.info("--- Start IOR 2 ---")
        self.run_ior_collect_error(
            job_num=job_num_keep, results=ior_results, file_name="test_file_3",
            clients=agent_hosts)

        # Verify that there's no error this time.
        self.log.info("--- IOR results 2 ---")
        self.log.info(ior_results[job_num_keep])
        ior_error = ior_results[job_num_keep][-1]
        if ior_error:
            errors.append("Error found in second IOR run! {}".format(ior_error))

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")
