#!/usr/bin/python3
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from server_utils import ServerFailed
from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from test_utils_base import CallbackHandler
from general_utils import get_default_config_file
from dmg_utils import get_dmg_command


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
    def get_group_info(hosts, svr_config_file=None, dmg_config_file=None,
                       svr_config_temp=None, dmg_config_temp=None):
        """Get the server group information.

        Args:
            hosts (list): list of hosts
            svr_config_file (str, optional): daos_server configuration file name
                and path. Defaults to None.
            dmg_config_file (str, optional): dmg configuration file name and
                path. Defaults to None.
            svr_config_temp (str, optional): file name and path used to generate
                the daos_server configuration file locally and copy it to all
                the hosts using the config_file specification. Defaults to None.
            dmg_config_temp (str, optional): file name and path used to generate
                the dmg configuration file locally and copy it to all the hosts
                using the config_file specification. Defaults to None.

        Returns:
            dict: a dictionary identifying the hosts and access points for the
                server group dictionary
        """
        return {
            "hosts": hosts,
            "access_points": hosts[:1],
            "svr_config_file": svr_config_file,
            "dmg_config_file": dmg_config_file,
            "svr_config_temp": svr_config_temp,
            "dmg_config_temp": dmg_config_temp
        }

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

    def validate_pool_destroy(self, hosts, case, exception_expected=False,
                              new_dmg=None):
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

        if new_dmg:
            self.log.info("Re-assigning new_dmg to self.pool.dmg")
            self.pool.dmg = new_dmg

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
        self.log.info("Restoring the pool's valid uuid: %s", valid_uuid)
        self.pool.uuid = valid_uuid

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

        # Prepare and configure dmg config files for a.
        dmg_config_file_a = get_default_config_file(name="control_a")
        dmg_config_temp_a = self.get_config_file(
            name=server_group_a, command="dmg", path=self.test_dir)

        # Prepare server group info with corresponding dmg config file.
        group_info_a = self.get_group_info(
            hosts=[self.hostlist_servers[0]], dmg_config_file=dmg_config_file_a,
            dmg_config_temp=dmg_config_temp_a)

        # Put everything into a dictionary and start server a.
        server_groups_a = {
            server_group_a: group_info_a,
        }
        self.start_servers(server_groups=server_groups_a)

        self.add_pool(connect=False)

        # Get dmg_i instance that uses daos_control_i.yml. Server group is i.
        cert_dir = os.path.join(os.sep, "etc", "daos", "certs")
        dmg_config_file_i = get_default_config_file(name="control_i")
        dmg_config_temp_i = self.get_config_file(
            name=server_group_i, command="dmg", path=self.test_dir)
        dmg_i = get_dmg_command(
            group=server_group_i, cert_dir=cert_dir, bin_dir=self.bin,
            config_file=dmg_config_file_i, config_temp=dmg_config_temp_i)

        # Update the second server manager's hostlist from "localhost" to
        # daos_server_a's hostname.
        dmg_i.hostlist = self.hostlist_servers[0]

        # Try destroying the pool in server a with dmg_i. Should fail because
        # of the group name mismatch.
        case_i = "Pool is in a, hostlist is a, and name is i."
        self.validate_pool_destroy(
            hosts=[self.hostlist_servers[0]], case=case_i,
            exception_expected=True, new_dmg=dmg_i)

        # Try destroying the pool in server a with the dmg that uses
        # daos_control_a.yml. Should pass.
        case_a = "Pool is in a, hostlist is a, and name is a."
        self.validate_pool_destroy(
            hosts=[self.hostlist_servers[0]], case=case_a,
            exception_expected=False, new_dmg=self.server_managers[0].dmg)

    def test_destroy_wrong_group(self):
        """Test destroying a pool with wrong server group.

        Test Steps:
        Two servers run with different group names, daos_server_a and
        daos_server_b. A pool is created on daos_server_a. Try destroying this
        pool specifying daos_server_b.

        We create a third dmg config file and specify the wrong server name in
        the name field and the right hostlist as below.

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

        # Prepare and configure dmg config files for a and b.
        dmg_config_file_a = get_default_config_file(name="control_a")
        dmg_config_temp_a = self.get_config_file(
            name=server_group_a, command="dmg", path=self.test_dir)
        dmg_config_file_b = get_default_config_file(name="control_b")
        dmg_config_temp_b = self.get_config_file(
            name=server_group_b, command="dmg", path=self.test_dir)

        # Prepare server group info with corresponding dmg config file.
        group_info_a = self.get_group_info(
            hosts=[self.hostlist_servers[0]], dmg_config_file=dmg_config_file_a,
            dmg_config_temp=dmg_config_temp_a)
        group_info_b = self.get_group_info(
            hosts=[self.hostlist_servers[1]], dmg_config_file=dmg_config_file_b,
            dmg_config_temp=dmg_config_temp_b)

        # Put everything into a dictionary and start server a and b.
        server_groups_a_b = {
            server_group_a: group_info_a,
            server_group_b: group_info_b
        }
        self.start_servers(server_groups=server_groups_a_b)

        self.add_pool(connect=False)

        # Get dmg_c instance that uses daos_control_c.yml. Server group is b.
        cert_dir = os.path.join(os.sep, "etc", "daos", "certs")
        dmg_config_file_c = get_default_config_file(name="control_c")
        dmg_config_temp_c = self.get_config_file(
            name=server_group_b, command="dmg", path=self.test_dir)
        dmg_c = get_dmg_command(
            group=server_group_b, cert_dir=cert_dir, bin_dir=self.bin,
            config_file=dmg_config_file_c, config_temp=dmg_config_temp_c)

        # Update the third server manager's hostlist from "localhost" to
        # daos_server_a's hostname.
        dmg_c.hostlist = self.hostlist_servers[:1]

        # Try destroying the pool in server a with dmg_c. Should fail because
        # of the group name mismatch.
        case_c = "Pool is in a, hostlist is a, and name is b."
        self.validate_pool_destroy(
            hosts=[self.hostlist_servers[0]], case=case_c,
            exception_expected=True, new_dmg=dmg_c)

        # Try destroying the pool in server a with the dmg that uses
        # daos_control_b.yml. Should fail because the pool doesn't exist in b.
        case_b = "Pool is in a, hostlist is b, and name is b."
        self.validate_pool_destroy(
            hosts=[self.hostlist_servers[0]], case=case_b,
            exception_expected=True, new_dmg=self.server_managers[1].dmg)

        # Try destroying the pool in server a with the dmg that uses
        # daos_control_a.yml. Should pass.
        case_a = "Pool is in a, hostlist is a, and name is a."
        self.validate_pool_destroy(
            hosts=[self.hostlist_servers[0]], case=case_a,
            exception_expected=False, new_dmg=self.server_managers[0].dmg)

        # Commented out due to DAOS-3836.
        # self.assertTrue(
        #    self.pool.check_files(group_hosts[group_names[0]]),
        #    "Pool UUID {} not dected in server group {}".format(
        #        self.pool.uuid, group_names[0]))
        # self.assertFalse(
        #    self.pool.check_files(group_hosts[group_names[1]]),
        #    "Pool UUID {} detected in server group {}".format(
        #        self.pool.uuid, group_names[1]))

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
