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
from general_utils import report_errors, get_host_data
from telemetry_utils import TelemetryUtils
from command_utils_base import CommandFailure


class AgentFailure(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify agent failure is properly handled.

    :avocado: recursive
    """
    def get_journalctl(self, hosts, since, until, journalctl_type):
        """Run the journalctl on the hosts.

        Args:
            hosts (list): List of hosts to run journalctl.
            since (str): Start time to search the log.
            until (str): End time to search the log.
            journalctl_type (str): String to search in the log. -t param for journalctl.

        Returns:
            list: a list of dictionaries containing the following key/value pairs:
                "hosts": NodeSet containing the hosts with this data
                "data":  data requested for the group of hosts

        """
        command = ("sudo /usr/bin/journalctl --system -t {} --since=\"{}\" "
                   "--until=\"{}\"".format(journalctl_type, since, until))
        err = "Error gathering system log events"
        results = get_host_data(
            hosts=hosts, command=command, text="journalctl", error=err)

        return results

    def run_custom_ior_cmd(self, results, job_num, file_name):
        """Run IOR command.

        Args:
            results (dict): A dictionary object to store the ior metrics
            job_num (int): Assigned job number
            file_name (str): File name used for self.ior_cmd.test_file.
        """
        self.ior_cmd.set_daos_params(self.server_group, self.pool, self.container.uuid)
        testfile = os.path.join("/", file_name)
        self.ior_cmd.test_file.update(testfile)

        manager = self.get_ior_job_manager_command()
        manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)

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
        IOR, the file write completes successfully, but the pool disconnect at the end
        fails, so we get the error message that includes -1005. This step is more like a
        verification of the test itself rather than the product.
        5. Verify daos_server doesn’t have the pool handle opened. Call:
        dmg telemetry metrics query --metrics=engine_pool_pool_handles
        that shows the number of open pool handle. Verify that it's 0 after the agent is
        stopped and IOR is finished. This step verifies the isolation of the failure. In
        this case, daos_agent was stopped, but daos_server can detect it and close the
        pool handle.
        6. Verify journalctl shows the log that the agent is stopped. Call:
        journalctl --system -t daos_agent --since <before> --until <after>
        This step verifies that DAOS, or daos_agent process in this case, prints useful
        logs for the user to troubleshoot the issue, which in this case the application
        can’t be used.
        7. Restart daos_agent.
        8. Run IOR again. It should succeed this time without any error. This step
        verifies that DAOS can recover from the fault with minimal human intervention.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=deployment
        :avocado: tags=agent_failure
        """
        # 1. Create a pool and a container.
        self.add_pool()
        self.add_container(self.pool)

        # 2. Run IOR.
        ior_results = {}
        job_num = 1
        self.log.info("Run IOR with thread")
        job = threading.Thread(
            target=self.run_custom_ior_cmd, args=[ior_results, job_num, "test_file_1"])

        self.log.info("Start IOR 1 (thread)")
        job.start()

        # We need to stop daos_agent while IOR is running, so need to wait for a few
        # seconds for IOR to start.
        self.log.info("Sleep 5 sec")
        time.sleep(5)

        errors = []

        # 3. Stop daos_agent process while IOR is running.
        since = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.log.info("Stopping agent")
        stop_agent_errors = self.stop_agents()
        for error in stop_agent_errors:
            self.log.debug(error)
            errors.append(error)
        until = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

        # Wait until the IOR thread ends.
        job.join()

        # 4. Verify the error from the IOR command.
        self.log.info("--- IOR results 1 ---")
        self.log.info(ior_results)
        ior_error = ior_results[job_num][-1]
        if "-1005" not in ior_error:
            errors.append("-1005 is not in IOR error! {}".format(ior_error))

        # 5. Verify daos_server doesn’t have the pool handle opened.
        # This metric returns the number of open pool handles.
        metrics = "engine_pool_pool_handles"
        telemetry = TelemetryUtils(self.get_dmg_command(), self.server_managers[0].hosts)
        telemetry = TelemetryUtils(
            self.get_dmg_command(), self.server_managers[0].hosts)
        info = telemetry.get_metrics(name=metrics)
        open_count = info[self.server_managers[0].hosts[0]][metrics]["metrics"][0][
            "value"]
        if open_count != 0:
            errors.append("Unexpected number of open pool handle! {}".format(open_count))

        # 6. Verify journalctl shows the log that the agent is stopped.
        results = self.get_journalctl(
            hosts=self.hostlist_servers, since=since, until=until,
            journalctl_type="daos_agent")
        self.log.info("journalctl results = %s", results)
        if "shutting down" not in results[0]["data"]:
            msg = "Agent shut down message not found in journalctl! Output = {}".format(
                results)
            errors.append(msg)

        # 7. Restart agent.
        self.log.info("Restart agent")
        self.start_agent_managers()

        # 8. Run IOR again.
        self.log.info("Start IOR 2")
        self.run_custom_ior_cmd(
            job_num=job_num, results=ior_results, file_name="test_file_2")

        # Verify that there's no error this time.
        self.log.info("--- IOR results 2 ---")
        self.log.info(ior_results)
        ior_error = ior_results[job_num][-1]
        if ior_error:
            errors.append("Error found in second IOR run! {}".format(ior_error))

        report_errors(test=self, errors=errors)
