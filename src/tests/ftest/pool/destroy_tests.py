#!/usr/bin/python3
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from server_utils import ServerFailed
from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from test_utils_base import CallbackHandler
from general_utils import create_string_buffer, get_default_config_file
from command_utils_base import CommandFailure

class DestroyTests(TestWithServers):
    """Tests DAOS pool removal.

    :avocado: recursive
    """
    def get_group(self, name, hosts):
        """Get the server group dictionary.

        This provides the 'server_groups' argument for the self.start_servers()
        method.

        Args:
            name (str): the server group name
            hosts (list): list of hosts to include in the server group

        Returns:
            dict: the the server group argument for the start_servers()
                method

        """
        return {name: self.get_group_info(hosts)}

    @staticmethod
    def get_group_info(hosts):
        """Get the server group information.

        Args:
            hosts (list): list of hosts

        Returns:
            dict: a dictionary identifying the hosts and access points for the
                server group dictionary

        """
        return {"hosts": hosts, "access_points": [hosts[0]]}

    def execute_test(self, hosts, group_name, case, exception_expected=False):
        """Execute the pool destroy test.

        Args:
            hosts (list): hosts running servers serving the pool
            group_name (str): server group name
            case (str): pool description message
            exception_expected (bool, optional): is an exception expected to be
                raised when destroying the pool. Defaults to False.
        """
        # Start servers with the server group
        self.start_servers(self.get_group(group_name, hosts))

        # Validate the creation of a pool
        self.validate_pool_creation(hosts, group_name)

        # Validate pool destruction
        self.validate_pool_destroy(hosts, case, exception_expected)

    def validate_pool_creation(self, hosts, group_name):
        # pylint: disable=unused-argument
        """Validate the creation of a pool on the specified list of hosts.

        Args:
            hosts (list): hosts running servers serving the pool
            group_name (str): server group name
        """
        # Create a pool
        self.log.info("Create a pool")
        self.add_pool(create=False)
        self.pool.create()
        self.log.info("Pool UUID is %s", self.pool.uuid)

        # Commented out due to DAOS-3836. Remove pylint disable when fixed.
        # # Check that the pool was created
        # self.assertTrue(
        #    self.pool.check_files(hosts),
        #    "Pool data not detected on servers before destroy")

    def validate_pool_destroy(self, hosts, case, exception_expected=False):
        # pylint: disable=unused-argument
        """Validate a pool destroy.

        Args:
            hosts (list): hosts running servers serving the pool
            case (str): pool description message
            exception_expected (bool, optional): is an exception expected to be
                raised when destroying the pool. Defaults to False.
        """
        exception_detected = False
        saved_uuid = self.pool.uuid
        self.log.info("Attempting to destroy pool %s", case)
        try:
            self.pool.destroy(0)
        except TestFail as result:
            exception_detected = True
            if exception_expected:
                self.log.info(
                    "Expected exception - destroying pool %s: %s",
                    case, result)
            else:
                self.log.error(
                    "Unexpected exception - destroying pool %s: %s",
                    case, result)

        if not exception_detected and exception_expected:
            # The pool-destroy did not raise an exception as expected
            self.log.error(
                "Exception did not occur - destroying pool %s", case)

        # Restore the valid server group and check if valid pool still exists
        self.pool.uuid = saved_uuid
        # Commented out due to DAOS-3836. Remove pylint disable when fixed.
        # if exception_detected:
        #    self.log.info(
        #        "Check pool data still exists after a failed pool destroy")
        #    self.assertTrue(
        #        self.pool.check_files(hosts),
        #        "Pool data was not detected on servers after "
        #        "failing to destroy a pool {}".format(case))
        # else:
        #    self.log.info(
        #        "Check pool data does not exist after the pool destroy")
        #    self.assertFalse(
        #        self.pool.check_files(hosts),
        #        "Pool data was detected on servers after "
        #        "destroying a pool {}".format(case))

        self.assertEqual(
            exception_detected, exception_expected,
            "No exception when deleting a pool with invalid server group")

    def prepare_server(self, server_group, server_info, ctrl_file_suffix):
        """Add server manager and configure the manager.

        Add server manager will create a dmg instance and write the dmg config
        file based on the ctrl_file_suffix. If the variable is control_a, the
        file name will be daos_control_a.yml. At this point, the hostlist is
        "localhost". It'll be overwritten to the value in server_info, hosts I
        believe, when the server is started.

        Configure manager will do something that's not relevant to this test,
        but it's a necessary step to run the server. It'll create a directory to
        store logs, certs, etc.

        Args:
            server_group (str): Server group name. e.g., daos_server_a
            server_info (dict): Dictionary of hosts and access_points.
            ctrl_file_suffix (str): dmg config file name we want to use. It'll
            be added after "daos".
        """
        # Prepare running add_server_manager by creating the config file and the
        # temporary config file.
        dmg_config_file = get_default_config_file(name=ctrl_file_suffix)
        dmg_config_temp = self.get_config_file(
            name=server_group, command="dmg", path=self.test_dir)

        # Create a server manager.
        self.add_server_manager(
            group=server_group, dmg_config_file=dmg_config_file,
            dmg_config_temp=dmg_config_temp)

        # Configure the server manager we just created.
        self.configure_manager(
            name="server", manager=self.server_managers[-1],
            hosts=server_info["hosts"], slots=self.hostfile_servers_slots,
            access_points=server_info["access_points"])

    def test_destroy_single(self):
        """Test destroying a pool created on a single server.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=pool,destroy,destroy_single
        """
        hostlist_servers = self.hostlist_servers[:1]
        setid = self.params.get("setname", '/run/setnames/validsetname/')

        # Attempt to destroy a pool
        self.execute_test(hostlist_servers, setid, "with a single server")

    def test_destroy_multi(self):
        """Test destroying a pool created on two servers.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=pool,destroy,destroy_multi
        """
        hostlist_servers = self.hostlist_servers[:2]
        setid = self.params.get("setname", '/run/setnames/validsetname/')

        # Attempt to destroy a pool
        self.execute_test(hostlist_servers, setid, "with multiple servers")

    def test_destroy_single_loop(self):
        """Destroy and recreate pool multiple times.

        Test destroy and recreate one right after the other multiple times
        Should fail.

        :avocado: tags=all,full_regression
        :avocado: tags=pool,destroy,destroy_single_loop
        """
        hostlist_servers = self.hostlist_servers[:1]

        # Start servers
        self.start_servers(self.get_group(self.server_group, hostlist_servers))

        counter = 0
        while counter < 10:
            counter += 1

            # Create a pool
            self.validate_pool_creation(hostlist_servers, self.server_group)

            # Attempt to destroy the pool
            self.validate_pool_destroy(
                hostlist_servers,
                "with a single server - pass {}".format(counter))

    def test_destroy_multi_loop(self):
        """Test destroy on a large (relative) number of servers.

        :avocado: tags=all,full_regression
        :avocado: tags=pool,destroy,destroy_multi_loop
        """
        hostlist_servers = self.hostlist_servers[:6]

        # Start servers
        self.start_servers(self.get_group(self.server_group, hostlist_servers))

        counter = 0
        while counter < 10:
            counter += 1

            # Create a pool
            self.validate_pool_creation(hostlist_servers, self.server_group)

            # Attempt to destroy the pool
            self.validate_pool_destroy(
                hostlist_servers,
                "with multiple servers - pass {}".format(counter))

    def test_destroy_invalid_uuid(self):
        """Test destroying a pool uuid that doesn't exist.

        :avocado: tags=all,full_regression
        :avocado: tags=pool,destroy,destroy_invalid_uuid
        """
        hostlist_servers = self.hostlist_servers[:1]
        setid = self.params.get("setname", '/run/setnames/validsetname/')

        # Start servers
        self.start_servers(self.get_group(setid, hostlist_servers))

        # Create a pool
        self.validate_pool_creation(hostlist_servers, setid)

        # Change the pool uuid
        valid_uuid = self.pool.uuid
        invalid_uuid = "81ef94d7-a59d-4a5e-935b-abfbd12f2105"
        self.pool.uuid = invalid_uuid

        # Attempt to destroy the pool with an invalid UUID
        self.validate_pool_destroy(
            hosts=hostlist_servers,
            case="with an invalid UUID {}".format(
                self.pool.pool.get_uuid_str()),
            exception_expected=True)

        # Restore the valid uuid to allow tearDown() to pass
        self.pool.uuid = valid_uuid

    def test_destroy_wrong_group(self):
        """Test destroying a pool with wrong server group.

        Test Steps:
        Two servers run with different group names, daos_server_a and
        daos_server_b. A pool is created on daos_server_a. Try destroying this
        pool specifying daos_server_b.

        We create a third dmg config file and specify the wrong server name in the
        name field and the right hostlist as below.

        daos_control_a.yml
        hostlist:
        - wolf-a
        name: daos_server_a

        daos_control_b.yml
        hostlist:
        - wolf-b
        name: daos_server_b

        daos_control_c.yml
        hostlist:
        - wolf-a
        name: daos_server_b

        We'll use daos_control_c.yml during dmg pool destroy and verify that it
        fails. It should show something like:
        "request system does not match running system (daos_server_b !=
        daos_server_a)"

        :avocado: tags=all,full_regression
        :avocado: tags=pool,destroy,destroy_wrong_group
        """
        server_group_a = self.server_group + "_a"
        server_group_b = self.server_group + "_b"
        group_info_a = self.get_group_info([self.hostlist_servers[0]])
        group_info_b = self.get_group_info([self.hostlist_servers[1]])

        # Prepare daos_server_a and daos_server_b server managers.
        self.prepare_server(server_group_a, group_info_a, "control_a")
        self.prepare_server(server_group_b, group_info_b, "control_b")

        # Run daos_server_a on one host and daos_server_b on another host.
        self.start_server_managers()

        # Create the third server manager that contains the dmg and the config
        # file that we want to test with.
        self.prepare_server(server_group_b, group_info_a, "control_c")
        dmg_c = self.server_managers[2].dmg

        # Update the third server manager's hostlist from "localhost" to
        # daos_server_a's hostname.
        dmg_c.hostlist = self.hostlist_servers[0]
        # Write the updated value to daos_control_c.yml
        dmg_c.create_yaml_file()

        # Create a pool in daos_server_a. The "index" argument defines which
        # server manager we want to use. We want "a", which is the first
        # manager. self.add_pool() will do the same thing, but we want to be
        # explicit.
        pools = []
        pools.append(self.get_pool(create=False, index=0))
        pools[0].create()

        # Commented out due to DAOS-3836.
        # self.assertTrue(
        #    self.pool.check_files(group_hosts[group_names[0]]),
        #    "Pool UUID {} not dected in server group {}".format(
        #        self.pool.uuid, group_names[0]))
        # self.assertFalse(
        #    self.pool.check_files(group_hosts[group_names[1]]),
        #    "Pool UUID {} detected in server group {}".format(
        #        self.pool.uuid, group_names[1]))

        # Attempt to delete the pool from the wrong server group - should fail
        try:
            dmg_c.pool_destroy(pools[0].uuid)
            self.fail("dmg pool destroy succeeded!")
        except CommandFailure as result:
            self.log.info(
                "Expected exception - dmg pool destroy failed. %s", result)

        # # Attempt to delete the pool from the right server group - should pass
        dmg_a = self.server_managers[0].dmg
        try:
            dmg_a.pool_destroy(pools[0].uuid)
        except CommandFailure as err:
            self.fail("dmg pool destroy failed! %s", err)

    def test_destroy_invalid_group(self):
        """Test destroying a pool with invalid server group.

        Test Steps:
        Run a server with group name "daos_server_a" and create a pool on it.
        Try destroying this pool specifying daos_server_i. "i" for invalid.

        We create a second dmg config file and specify the invalid server name
        in the name field and the right hostlist as below.

        daos_control_a.yml
        hostlist:
        - wolf-a
        name: daos_server_a

        daos_control_i.yml
        hostlist:
        - wolf-a
        name: daos_server_i

        We'll use daos_control_i.yml during dmg pool destroy and verify that it
        fails. It should show something like:
        "request system does not match running system (daos_server_i !=
        daos_server_a)"

        :avocado: tags=all,full_regression
        :avocado: tags=pool,destroy,destroy_invalid_group
        """
        server_group_a = self.server_group + "_a"
        server_group_i = self.server_group + "_i"
        group_info_a = self.get_group_info([self.hostlist_servers[0]])

        # Prepare daos_server_a server manager.
        self.prepare_server(server_group_a, group_info_a, "control_a")

        # Run daos_server_a.
        self.start_server_managers()

        # Create the third server manager that contains the dmg and the config
        # file that we want to test with.
        self.prepare_server(server_group_i, group_info_a, "control_i")

        dmg_i = self.server_managers[1].dmg

        # Update the second server manager's hostlist from "localhost" to
        # daos_server_a's hostname.
        dmg_i.hostlist = self.hostlist_servers[0]
        # Write the updated value to daos_control_i.yml
        dmg_i.create_yaml_file()

        # Create a pool in daos_server_a. The "index" argument defines which
        # server manager we want to use. We want "a", which is the first
        # manager. self.add_pool() will do the same thing, but we want to be
        # explicit.
        pools = []
        pools.append(self.get_pool(create=False, index=0))
        pools[0].create()

        # Attempt to delete the pool from the invalid server group - should fail
        try:
            dmg_i.pool_destroy(pools[0].uuid)
            self.fail("dmg pool destroy succeeded!")
        except CommandFailure as result:
            self.log.info(
                "Expected exception - dmg pool destroy failed. %s", result)

        # # Attempt to delete the pool from the right server group - should pass
        dmg_a = self.server_managers[0].dmg
        try:
            #dmg_a.pool_destroy(self.pool[0].uuid)
            dmg_a.pool_destroy(pools[0].uuid)
        except CommandFailure as err:
            self.fail("dmg pool destroy failed! %s", err)

    def test_destroy_connected(self):
        """Destroy pool with connected client.

        Test destroying a pool that has a connected client with force == false.
        Should fail.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=pool,destroy,destroy_connected
        """
        hostlist_servers = self.hostlist_servers[:1]

        # Start servers
        self.start_servers(self.get_group(self.server_group, hostlist_servers))

        # Create the pool
        self.validate_pool_creation(hostlist_servers, self.server_group)

        # Connect to the pool
        self.assertTrue(
            self.pool.connect(), "Pool connect failed before destroy")

        # Destroy pool with force unset
        self.log.info("Attempting to destroy a connected pool")
        exception_detected = False
        try:
            self.pool.destroy(force=0, disconnect=0)
        except TestFail as result:
            exception_detected = True
            self.log.info(
                "Expected exception - destroying connected pool: %s",
                str(result))

        if not exception_detected:
            # The pool-destroy did not raise an exception as expected
            self.log.error(
                "Exception did not occur - destroying connected pool")

            # Prevent attempting to delete the pool in tearDown()
            self.pool.pool = None

        # Commented out due to DAOS-3836.
        # self.log.info("Check if files still exist")
        # self.assertTrue(
        #    self.pool.check_files(hostlist_servers),
        #    "Pool UUID {} should not be removed when connected".format(
        #        self.pool.uuid))

        self.assertTrue(
            exception_detected, "No exception when deleting a connected pool")

    def test_forcedestroy_connected(self):
        """Forcibly destroy pool with connected client.

        Test destroying a pool that has a connected client with force == true.
        Should pass.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=pool,destroy,force_destroy_connected
        """
        hostlist_servers = self.hostlist_servers[:1]

        # Start servers
        self.start_servers(self.get_group(self.server_group, hostlist_servers))

        # Create the pool
        self.validate_pool_creation(hostlist_servers, self.server_group)

        # Connect to the pool
        self.assertTrue(
            self.pool.connect(), "Pool connect failed before destroy")

        # Destroy pool with force set
        self.log.info("Attempting to forcibly destroy a connected pool")
        exception_detected = False
        try:
            self.pool.destroy(force=1, disconnect=0)

        except TestFail as result:
            exception_detected = True
            self.log.info(
                "Unexpected exception - destroying connected pool: %s",
                str(result))

        finally:
            # Prevent attempting to delete the pool in tearDown()
            self.pool.pool = None
            if exception_detected:
                self.fail("Force destroying connected pool failed")

    def test_destroy_withdata(self):
        """Destroy Pool with data.

        Test destroy and recreate one right after the other multiple times
        Should fail.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=pool,destroy,destroy_with_data
        """
        hostlist_servers = self.hostlist_servers[:1]

        # Start servers
        self.start_servers(self.get_group(self.server_group, hostlist_servers))

        # Attempt to destroy the pool with an invalid server group name
        self.validate_pool_creation(hostlist_servers, self.server_group)

        # Connect to the pool
        self.assertTrue(
            self.pool.connect(), "Pool connect failed before destroy")

        # Create a container
        self.add_container(self.pool)
        self.log.info(
            "Writing 4096 bytes to the container %s", self.container.uuid)
        self.container.write_objects(obj_class="OC_S1")

        # Attempt to destroy a connected pool with a container with data
        self.log.info("Attempting to destroy a connected pool with data")
        exception_detected = False
        try:
            self.pool.destroy(force=0, disconnect=0)
        except TestFail as result:
            exception_detected = True
            self.log.info(
                "Expected exception - destroying connected pool with data: %s",
                str(result))

        if not exception_detected:
            # The pool-destroy did not raise an exception as expected
            self.log.error(
                "Exception did not occur - destroying connected pool with "
                "data")

            # Prevent attempting to delete the pool in tearDown()
            self.pool.pool = None

        # Commented out due to DAOS-3836.
        # self.log.info("Check if files still exist")
        # self.assertTrue(
        #    self.pool.check_files(hostlist_servers),
        #    "Pool UUID {} should not be removed when connected".format(
        #        self.pool.uuid))

        self.assertTrue(
            exception_detected,
            "No exception when deleting a connected pool with data")
