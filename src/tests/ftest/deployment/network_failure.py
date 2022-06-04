#!/usr/bin/python
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time

from ior_test_base import IorTestBase
from ior_utils import IorCommand
from general_utils import report_errors, run_pcmd
from command_utils_base import CommandFailure
from job_manager_utils import get_job_manager
from network_utils import update_network_interface


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

    def tearDown(self):
        """Bring ib0 back up in case the test crashed in the middle."""
        if self.test_env == "ci":
            self.log.debug("Call ip link set before tearDown.")
            if self.network_down_host:
                self.update_network_interface(
                    interface=self.interface, state="up", host=self.network_down_host)

        super().tearDown()

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
        """
        command = "hostname -i"
        results = run_pcmd(hosts=self.hostlist_servers, command=command)
        self.log.info("hostname -i results = %s", results)

        return {result["stdout"][0]: str(result["hosts"]) for result in results}

    def verify_network_failure(self, ior_namespace, container_namespace):
        """Verify network failure can be recovered without intervention in DAOS side.

        1. Create a pool and a container. Create a container with or without redundancy
        factor based on container_namespace.
        2. Take down network interface of one of the engines, say ib0 of rank 0. hsn0 in
        Aurora.
        3. Run IOR with given object class.
        4. Bring up the network interface.
        5. Restart DAOS with dmg.
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
        self.network_down_host = self.hostlist_servers[0]
        self.log.info("network_down_host = %s", self.network_down_host)
        self.interface = self.params.get(
            "fabric_iface", "/run/server_config/servers/0/*")
        self.log.info("interface to update = %s", self.interface)

        if self.test_env == "ci":
            # wolf
            self.update_network_interface(
                interface=self.interface, state="down", host=self.network_down_host,
                errors=errors)
        else:
            # Aurora
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
            self.update_network_interface(
                interface=self.interface, state="up", host=self.network_down_host,
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
            self.pool.wait_for_rebuild(to_start=True, interval=5)
            self.pool.wait_for_rebuild(to_start=False, interval=10)

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
        """Jira ID: DAOS-10003

        Test rank failure without redundancy factor and SX object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,network_failure
        :avocado: tags=network_failure_wo_rf
        """
        self.verify_network_failure(
            ior_namespace="/run/ior_wo_rf/*",
            container_namespace="/run/container_wo_rf/*")

    def test_network_failure_with_rp(self):
        """Jira ID: DAOS-10003

        Test rank failure with redundancy factor and RP_2G1 object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,network_failure
        :avocado: tags=network_failure_with_rp
        """
        self.verify_network_failure(
            ior_namespace="/run/ior_with_rp/*",
            container_namespace="/run/container_with_rf/*")

    def test_network_failure_with_ec(self):
        """Jira ID: DAOS-10003

        Test rank failure with redundancy factor and EC_2P1G1 object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,network_failure
        :avocado: tags=network_failure_with_ec
        """
        self.verify_network_failure(
            ior_namespace="/run/ior_with_ec/*",
            container_namespace="/run/container_with_rf/*")

    def test_network_failure_isolation(self):
        """Jira ID: DAOS-10003

        Verify that network failure in a node where pool isn't created doesn't affect the
        connection.

        1. Determine the two ranks to create the pool and an interface to take down.
        2. Create a pool across two ranks on the same node.
        3. Create a container without redundancy factor.
        4. Take down the interface where the pool isn't created. This will simulate the
        case where there’s a network failure, but doesn’t affect the user because their
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
        :avocado: tags=network_failure_isolation
        """
        # 1. Determine the two ranks to create the pool and an interface to take down.
        # We'll create a pool on rank 0 and the other rank that's on the same node. Find
        # hostname of rank 0.
        rank_0_ip = None
        output = self.get_dmg_command().system_query()
        members = output["response"]["members"]
        for member in members:
            if member["rank"] == 0:
                rank_0_ip = member["addr"].split(":")[0]
                break
        self.log.info("rank 0 IP = %s", rank_0_ip)

        # Find the other rank that's on the same node as in rank 0. Call it rank_r.
        rank_r = None
        for member in members:
            if member["addr"].split(":")[0] == rank_0_ip and member["rank"] != 0:
                rank_r = member["rank"]
                break
        self.log.info("rank_r = %s", rank_r)

        # Find the hostname that's different from rank_0_ip. We'll take down the
        # interface on it.
        # dmg system query output gives IP address, but run_pcmd doesn't work with IP in
        # CI. In addition, in Aurora, it's easier to determine where to run the ip link
        # command if we know the hostname, so create the IP - hostname mapping.
        ip_to_host = self.create_ip_to_host()
        for member in members:
            ip_addr = member["addr"].split(":")[0]
            if rank_0_ip != ip_addr:
                self.network_down_host = ip_to_host[ip_addr]
                break
        self.log.info("network_down_host = %s", self.network_down_host)

        # 2. Create a pool across two ranks on the same node; 0 and rank_r. We have to
        # provide the size because we're using --ranks.
        self.add_pool(namespace="/run/pool_size_value/*", create=False)
        self.pool.target_list.update([0, rank_r])
        self.pool.create()

        # 3. Create a container without redundancy factor.
        self.container = []
        self.container.append(
            self.get_container(pool=self.pool, namespace="/run/container_wo_rf/*"))

        # 4. Take down the interface where the pool isn't created.
        errors = []
        self.interface = self.params.get(
            "fabric_iface", "/run/server_config/servers/0/*")

        # wolf
        if self.test_env == "ci":
            self.update_network_interface(
                interface=self.interface, state="down", host=self.network_down_host,
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
        if not self.container[0].verify_health(expected_health="HEALTHY"):
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
            self.update_network_interface(
                interface=self.interface, state="up", host=self.network_down_host,
                errors=errors)
        else:
            # Aurora
            command = "sudo ip link set {} {}".format(self.interface, "up")
            self.log.debug("## Call %s on %s", command, self.network_down_host)
            time.sleep(20)

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")
