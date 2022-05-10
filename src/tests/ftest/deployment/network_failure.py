#!/usr/bin/python
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from ior_test_base import IorTestBase
from ior_utils import IorCommand
from general_utils import report_errors, run_pcmd
from command_utils_base import CommandFailure
from job_manager_utils import get_job_manager


class NetworkFailureTest(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify network failure is properly handled and recovered.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Store the info needed in tearDown."""
        super().__init__(*args, **kwargs)
        self.network_down_host = None

    def tearDown(self):
        """Bring ib0 back up in case the test crashed in the middle."""
        self.log.debug("Call ip link set before tearDown.")
        if self.network_down_host:
            self.update_network_interface(
                interface="ib0", state="up", host=self.network_down_host)
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

    def update_network_interface(self, interface, state, host, errors=None):
        """Bring back or tear down the given network interface.

        Args:
            interface (str): Interface name such as ib0.
            state (str): "up" or "down".
            host (str): Host to update the interface.
            errors (list): List to store the error message, if the command fails.
                Defaults to None.
        """
        command = "sudo ip link set {} {}".format(interface, state)
        results = run_pcmd(hosts=[host], command=command)
        self.log.info("%s output = %s", command, results)
        if errors is not None and results[0]["exit_status"] != 0:
            errors.append("{} didn't return 0!".format(command))

    def verify_network_failure(self, ior_namespace, container_namespace):
        """Verify network failure can be recovered without intervention in DAOS side.

        1. Create a pool and a container. Create a container with or without redundancy
        factor based on container_namespace.
        2. Take down network interface of one of the engines, say ib0 of rank 0. hsn0 in
        Aurora.
        3. Run IOR with given object class. It should fail.
        4. Bring up the network interface.
        5. Run IOR again. It should work this time.
        6. To further verify the system, create another container.
        7. Run IOR to the new container. Should work.

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
        self.add_pool(namespace="/run/pool_size_ratio_80/*")
        self.add_container(pool=self.pool, namespace=container_namespace)

        # 2. Take down network interface of one of the engines. Use the first host.
        errors = []
        self.network_down_host = self.hostlist_servers[0]
        self.log.info("network_down_host = %s", self.network_down_host)
        interface = self.params.get("fabric_iface", "/run/server_config/servers/0/*")
        self.log.info("interface to update = %s", interface)
        self.update_network_interface(
            interface=interface, state="down", host=self.network_down_host,
            errors=errors)

        # 3. Run IOR with given object class. It should fail.
        job_num = 1
        ior_results = {}
        # IOR will not work, so we'll be waiting for the 20-sec Mpirun timeout.
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_1",
            pool=self.pool, container=self.container, namespace=ior_namespace,
            timeout=20)
        self.log.info(ior_results)
        if ior_results[job_num][0]:
            # Something is wrong with the test setup.
            errors.append("IOR worked while network is down!")

        # 4. Bring up the network interface.
        self.update_network_interface(
            interface="ib0", state="up", host=self.network_down_host, errors=errors)

        # 5. Run IOR again. It should work this time.
        job_num = 2
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_2",
            pool=self.pool, container=self.container, namespace=ior_namespace)
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        # 6. To further verify the system, create another container.
        self.add_container(pool=self.pool, namespace=container_namespace)

        # 7. Run IOR to the new container. Should work.
        job_num = 3
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_3",
            pool=self.pool, container=self.container, namespace=ior_namespace)
        self.verify_ior_worked(ior_results=ior_results, job_num=job_num, errors=errors)

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")

    def test_network_failure_wo_rf(self):
        """Jira ID: DAOS-10003.

        Test rank failure without redundancy factor and SX object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,network_failure
        :avocado: tags=network_failure_wo_rf
        """
        self.verify_network_failure(
            ior_namespace="/run/ior_wo_rf/*",
            container_namespace="/run/container_wo_rf/*")

    def test_network_failure_with_rp(self):
        """Jira ID: DAOS-10002.

        Test rank failure with redundancy factor and RP_2G1 object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,network_failure
        :avocado: tags=network_failure_with_rp
        """
        self.verify_network_failure(
            ior_namespace="/run/ior_with_rp/*",
            container_namespace="/run/container_with_rf/*")

    def test_network_failure_with_ec(self):
        """Jira ID: DAOS-10002.

        Test rank failure with redundancy factor and EC_2P1G1 object class. See
        verify_rank_failure() for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,network_failure
        :avocado: tags=network_failure_with_ec
        """
        self.verify_network_failure(
            ior_namespace="/run/ior_with_ec/*",
            container_namespace="/run/container_with_rf/*")

    def test_network_failure_isolation(self):
        """Jira ID: DAOS-10003.

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
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,network_failure
        :avocado: tags=network_failure_isolation
        """
        # 1. Determine the two ranks to create the pool and an interface to take down.
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

        # Find the hostname that's different from rank_0_host. We'll take down the
        # interface on it.
        for member in members:
            host = member["addr"].split(":")[0]
            if rank_0_host != host:
                self.network_down_host = host
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
        interface = self.params.get("fabric_iface", "/run/server_config/servers/0/*")
        self.update_network_interface(
            interface=interface, state="down", host=self.network_down_host,
            errors=errors)

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
        if not self.check_container_health(
                container=self.container[0], expected_health="HEALTHY"):
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
        self.update_network_interface(
            interface="ib0", state="up", host=self.network_down_host, errors=errors)

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")
