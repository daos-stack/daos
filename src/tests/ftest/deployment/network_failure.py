"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time
from collections import defaultdict
from ClusterShell.NodeSet import NodeSet

from ior_test_base import IorTestBase
from ior_utils import IorCommand
from general_utils import report_errors, run_pcmd
from command_utils_base import CommandFailure
from job_manager_utils import get_job_manager
from network_utils import update_network_interface
from dmg_utils import check_system_query_status


class NetworkFailureTest(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify network failure is properly handled and recovered.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Store the info used during the test and the tearDown."""
        super().__init__(*args, **kwargs)
        self.network_down_host = None
        self.interface = None
        self.test_env = self.params.get("test_environment", "/run/*")

    def pre_tear_down(self):
        """Bring ib0 back up in case the test crashed in the middle."""
        error_list = []
        if self.test_env == "ci":
            self.log.debug("Call ip link set before tearDown.")
            if self.network_down_host:
                update_network_interface(
                    interface=self.interface, state="up", hosts=self.network_down_host,
                    errors=error_list)
        return error_list

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
        ior_cmd.set_daos_params(self.server_group, pool, container.identifier)
        ior_cmd.test_file.update(os.path.join(os.sep, file_name))

        manager = get_job_manager(
            test=self, job=ior_cmd, subprocess=self.subprocess, timeout=timeout)
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
            results[job_num] = [False, f"IOR failed: {error}"]

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
            errors.append(f"Error found in second IOR run! {ior_error}")

    def create_ip_to_host(self):
        """Create a dictionary of IP address to hostname of the server nodes.

        Returns:
            dict: Dictionary of IP address to hostname (NodeSet representation) of all
                server nodes.

        """
        command = "hostname -i"
        results = run_pcmd(hosts=self.hostlist_servers, command=command)
        self.log.info("hostname -i results = %s", results)

        ip_to_host = {}
        for result in results:
            ips_str = result["stdout"][0]
            # There may be multiple IP addresses for one host.
            ip_addresses = ips_str.split()
            for ip_address in ip_addresses:
                ip_to_host[ip_address] = NodeSet(str(result["hosts"]))

        return ip_to_host

    @staticmethod
    def create_host_to_ranks(ip_to_host, system_query_members):
        """Create a dictionary of hostname to ranks.

        Args:
            ip_to_host (dict): create_ip_to_host output.
            system_query_members (dict): Contents of dmg system query accessed with
                ["response"]["members"].

        Returns:
            dict: Hostname to ranks mapping. Hostname (key) is string. Ranks (value) is
                list of int.

        """
        host_to_ranks = defaultdict(list)
        for member in system_query_members:
            ip_addr = member["addr"].split(":")[0]
            host = str(ip_to_host[ip_addr])
            rank = member["rank"]
            host_to_ranks[host].append(rank)

        return host_to_ranks

    def wait_for_ranks_to_join(self):
        """Wait for all ranks to join.

        Returns:
            bool: False if any of the rank's state is in the failed state (unknown,
                excluded, errored, unresponsive) after waiting for 2 min. True otherwise.

        """
        time.sleep(60)

        for _ in range(12):
            time.sleep(10)
            if check_system_query_status(self.get_dmg_command().system_query()):
                self.log.info("All ranks are joined after updating the interface.")
                return True
            self.log.info("One or more servers crashed. Check system query again.")

        return False

    def verify_network_failure(self, ior_namespace, container_namespace):
        """Verify network failure can be recovered with some user interventions with DAOS.

        1. Create a pool and a container. Create a container with or without redundancy
        factor based on container_namespace.
        2. Take down network interface of one of the engines, say ib0 of rank 0. hsn0 in
        Aurora.
        3. Run IOR with given object class.
        4. Bring up the network interface.
        5. Restart DAOS with dmg system stop and start.
        6. Call dmg pool query -b to find the disabled ranks.
        7. Call dmg pool reintegrate --rank=<rank> one rank at a time to enable all
        ranks. Wait for rebuild after calling the command.
        8. Run IOR again. It should work this time.
        9. To further verify the system, create another container.
        10. Run IOR to the new container. Should work.

        Note that I'm not sure about the usefulness of testing different object classes
        and redundancy factors. We probably have to understand how data are exchanged
        among the ranks based on the object class.

        Args:
            ior_namespace (str): Yaml namespace that defines the object class used for
                IOR.
            container_namespace (str): Yaml namespace that defines the container
                redundancy factor.
        """
        # 1. Create a pool and a container.
        self.container = []
        self.add_pool(namespace="/run/pool_size_ratio_80/*")
        self.container.append(
            self.get_container(pool=self.pool, namespace=container_namespace))

        # 2. Take down network interface of one of the engines. Use the first host.
        errors = []
        self.network_down_host = NodeSet(self.hostlist_servers[0])
        self.log.info("network_down_host = %s", self.network_down_host)
        self.interface = self.server_managers[0].get_config_value("fabric_iface")
        self.log.info("interface to update = %s", self.interface)

        if self.test_env == "ci":
            # wolf
            update_network_interface(
                interface=self.interface, state="down", hosts=self.network_down_host,
                errors=errors)
        else:
            # Aurora. Manually run the command.
            command = f"sudo ip link set {self.interface} down"
            self.log.debug("## Call %s on %s", command, self.network_down_host)
            time.sleep(20)

        # 3. Run IOR with given object class. It should fail.
        job_num = 1
        ior_results = {}
        # IOR will not work, so we'll be waiting for the Mpirun timeout.
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_1",
            pool=self.pool, container=self.container[0], namespace=ior_namespace,
            timeout=10)
        self.log.info(ior_results)

        # 4. Bring up the network interface.
        if self.test_env == "ci":
            # wolf
            update_network_interface(
                interface=self.interface, state="up", hosts=self.network_down_host,
                errors=errors)
        else:
            # Aurora. Manually run the command.
            command = f"sudo ip link set {self.interface} up"
            self.log.debug("## Call %s on %s", command, self.network_down_host)
            time.sleep(20)

        # 5. Restart DAOS with dmg.
        self.log.info("Wait for 5 sec for the network to come back up")
        time.sleep(5)
        dmg_cmd = self.get_dmg_command()
        # For debugging.
        dmg_cmd.system_query()
        self.log.info("Call dmg system stop")
        dmg_cmd.system_stop()
        self.log.info("Call dmg system start")
        dmg_cmd.system_start()

        # 6. Call dmg pool query -b to find the disabled ranks.
        output = dmg_cmd.pool_query(pool=self.pool.identifier, show_disabled=True)
        disabled_ranks = output["response"]["disabled_ranks"]
        self.log.info("Disabled ranks = %s", disabled_ranks)

        # 7. Call dmg pool reintegrate one rank at a time to enable all ranks.
        for disabled_rank in disabled_ranks:
            self.pool.reintegrate(rank=disabled_rank)
            self.pool.wait_for_rebuild_to_start(interval=5)
            self.pool.wait_for_rebuild_to_end(interval=10)

        # 8. Run IOR again. It should work this time.
        job_num = 2
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_2",
            pool=self.pool, container=self.container[0], namespace=ior_namespace)
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        # 6. To further verify the system, create another container.
        self.container.append(
            self.get_container(pool=self.pool, namespace=container_namespace))

        # 7. Run IOR to the new container. Should work.
        job_num = 3
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_3",
            pool=self.pool, container=self.container[1], namespace=ior_namespace)
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")

    def test_network_failure_wo_rf(self):
        """Jira ID: DAOS-10003.

        Test rank failure without redundancy factor and SX object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,network_failure,rebuild
        :avocado: tags=NetworkFailureTest,test_network_failure_wo_rf
        """
        self.verify_network_failure(
            ior_namespace="/run/ior_wo_rf/*",
            container_namespace="/run/container_wo_rf/*")

    def test_network_failure_with_rp(self):
        """Jira ID: DAOS-10003.

        Test rank failure with redundancy factor and RP_2G1 object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,network_failure,rebuild
        :avocado: tags=NetworkFailureTest,test_network_failure_with_rp
        """
        self.verify_network_failure(
            ior_namespace="/run/ior_with_rp/*",
            container_namespace="/run/container_with_rf/*")

    def test_network_failure_with_ec(self):
        """Jira ID: DAOS-10003.

        Test rank failure with redundancy factor and EC_2P1G1 object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,network_failure,rebuild
        :avocado: tags=NetworkFailureTest,test_network_failure_with_ec
        """
        self.verify_network_failure(
            ior_namespace="/run/ior_with_ec/*",
            container_namespace="/run/container_with_rf/*")

    def test_network_failure_isolation(self):
        """Jira ID: DAOS-10003.

        Verify that network failure in a node where pool isn't created doesn't affect the
        connection.

        1. Determine the four ranks to create the pool and an interface to take down.
        2. Create a pool across the four ranks on the two nodes.
        3. Create a container without redundancy factor.
        4. Take down the interface where the pool isn't created. This will simulate the
        case where there’s a network failure, but does not affect the user because their
        pool isn’t created on the failed node (assuming that everything else such as
        client node, engine, etc. are still working).
        5. Run IOR with oclass SX.
        6. Verify that IOR finishes successfully.
        7. Verify that the container Health is HEALTHY.
        8. To further verify that the pool isn’t affected, create a new container on the
        pool and run IOR.
        9. To clean up, bring up the network interface.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,network_failure
        :avocado: tags=NetworkFailureTest,test_network_failure_isolation
        """
        # 1. Determine the four ranks to create the pool and an interface to take down.
        # We'll create a pool on two ranks in hostlist_servers[0] and two ranks in
        # hostlist_servers[1].
        # There's no way to determine the mapping of hostname to ranks, but there's IP
        # address to rank mapping and IP address to hostname mapping, so we'll combine
        # them.
        # Call dmg system query, which contains IP address - Rank mapping.
        output = self.get_dmg_command().system_query()
        members = output["response"]["members"]

        # Create IP address - Hostname mapping by calling "hostname -i" on every server
        # node.
        ip_to_host = self.create_ip_to_host()
        # Using dmg system query output and ip_to_host, create Hostname - Ranks mapping.
        host_to_ranks = self.create_host_to_ranks(
            ip_to_host=ip_to_host, system_query_members=members)
        # Create a pool on the two ranks on two of the server nodes.
        target_list = []
        target_list.extend(host_to_ranks[self.hostlist_servers[0]])
        target_list.extend(host_to_ranks[self.hostlist_servers[1]])
        self.log.info("Ranks to create pool = %s", target_list)

        # We'll take down network on the last server node where the pool isn't created.
        self.network_down_host = NodeSet(self.hostlist_servers[2])
        self.log.info("network_down_host = %s", self.network_down_host)

        # 2. Create a pool across the four ranks on the two nodes. Use --nsvc=3. We have
        # to provide the size because we're using --ranks.
        self.add_pool(namespace="/run/pool_size_value/*", target_list=target_list)

        # 3. Create a container without redundancy factor.
        self.container = []
        self.container.append(
            self.get_container(pool=self.pool, namespace="/run/container_wo_rf/*"))

        # 4. Take down the interface where the pool isn't created.
        errors = []
        self.interface = self.server_managers[0].get_config_value("fabric_iface")

        # wolf
        if self.test_env == "ci":
            update_network_interface(
                interface=self.interface, state="down", hosts=self.network_down_host,
                errors=errors)
        else:
            # Aurora. Manually run the command.
            command = "sudo ip link set {} {}".format(self.interface, "down")
            self.log.debug("## Call %s on %s", command, self.network_down_host)
            time.sleep(20)

        # 5. Run IOR with oclass SX.
        ior_results = {}
        job_num = 1
        ior_namespace = "/run/ior_wo_rf/*"
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_1",
            pool=self.pool, container=self.container[0], namespace=ior_namespace)

        # 6. Verify that IOR worked.
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        # 7. Verify that the container Health is HEALTHY.
        if not self.container[0].verify_prop({"status": "HEALTHY"}):
            errors.append(
                "Container health isn't HEALTHY after taking ib0 down!")

        # 8. Create a new container on the pool and run IOR.
        self.container.append(
            self.get_container(pool=self.pool, namespace="/run/container_wo_rf/*"))

        # Run IOR and verify that it works.
        job_num = 2
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_2",
            pool=self.pool, container=self.container[1], namespace=ior_namespace)
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        # 9. Bring up the network interface.
        if self.test_env == "ci":
            # wolf
            update_network_interface(
                interface=self.interface, state="up", hosts=self.network_down_host,
                errors=errors)
        else:
            # Aurora. Manually run the command.
            command = "sudo ip link set {} {}".format(self.interface, "up")
            self.log.debug("## Call %s on %s", command, self.network_down_host)
            time.sleep(20)

        # Some ranks may be excluded after bringing up the network interface. Check if
        # all ranks are joined. If not, restart the servers and check again.
        dmg_command = self.get_dmg_command()

        # First, wait up to 60 sec for server(s) to crash. Whether a rank is marked as
        # dead is determined by SWIM, so we need to give some time for the protocol to
        # make the decision.
        count = 0
        server_crashed = False
        while count < 60:
            if not check_system_query_status(dmg_command.system_query()):
                server_crashed = True
                break
            count += 1
            time.sleep(1)

        # If server crash was detected, restart.
        if server_crashed:
            self.log.info("Not all ranks are joined. Restart the servers.")
            dmg_command.system_stop()
            dmg_command.system_start()

            # Now all ranks should be joined.
            if not self.wait_for_ranks_to_join():
                msg = ("One or more servers crashed after bringing up the network "
                       "interface!")
                errors.append(msg)

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")
