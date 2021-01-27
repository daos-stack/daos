#!/usr/bin/python2
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

from server_utils import ServerFailed
from apricot import TestWithServers, skipForTicket
from avocado.core.exceptions import TestFail
from test_utils_base import CallbackHandler
import ctypes


class DestroyTests(TestWithServers):
    """Tests DAOS pool removal.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for destroy."""

        self.setup_start_servers = False
        super(DestroyTests, self).setUp()

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
        self.start_servers({group_name: hosts})

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
        self.pool.name.value = group_name
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
                    case, str(result))
            else:
                self.log.error(
                    "Unexpected exception - destroying pool %s: %s",
                    case, str(result))

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

        :avocado: tags=all,medium,pr,daily_regression
        :avocado: tags=pool,destroy,destroysingle
        """
        hostlist_servers = self.hostlist_servers[:1]
        setid = self.params.get("setname", '/run/setnames/validsetname/')

        # Attempt to destroy a pool
        self.execute_test(hostlist_servers, setid, "with a single server")

    def test_destroy_multi(self):
        """Test destroying a pool created on two servers.

        :avocado: tags=all,medium,pr,daily_regression
        :avocado: tags=pool,destroy,destroymutli
        """
        hostlist_servers = self.hostlist_servers[:2]
        setid = self.params.get("setname", '/run/setnames/validsetname/')

        # Attempt to destroy a pool
        self.execute_test(hostlist_servers, setid, "with multiple servers")

    def test_destroy_single_loop(self):
        """Destroy and recreate pool multiple times.

        Test destroy and recreate one right after the other multiple times
        Should fail.

        :avocado: tags=all,medium,full_regression
        :avocado: tags=pool,destroy,destroysingleloop
        """
        hostlist_servers = self.hostlist_servers[:1]

        # Start servers
        self.start_servers({self.server_group: hostlist_servers})

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

        :avocado: tags=all,medium,full_regression
        :avocado: tags=pool,destroy,destroymutliloop
        """
        hostlist_servers = self.hostlist_servers[:6]

        # Start servers
        self.start_servers({self.server_group: hostlist_servers})

        counter = 0
        while counter < 10:
            counter += 1

            # Create a pool
            self.validate_pool_creation(hostlist_servers, self.server_group)

            # Attempt to destroy the pool
            self.validate_pool_destroy(
                hostlist_servers,
                "with multiple servers - pass {}".format(counter))

    @skipForTicket("DAOS-2739")
    def test_destroy_invalid_uuid(self):
        """Test destroying a pool uuid that doesn't exist.

        :avocado: tags=all,medium,full_regression
        :avocado: tags=pool,destroy,destroyinvaliduuid
        """
        hostlist_servers = self.hostlist_servers[:1]
        setid = self.params.get("setname", '/run/setnames/validsetname/')

        # Start servers
        self.start_servers({setid: hostlist_servers})

        # Create a pool
        self.validate_pool_creation(hostlist_servers, setid)

        # Change the pool uuid
        valid_uuid = self.pool.pool.uuid
        self.pool.pool.set_uuid_str("81ef94d7-a59d-4a5e-935b-abfbd12f2105")

        # Attempt to destroy the pool with an invalid UUID
        self.validate_pool_destroy(
            hostlist_servers,
            "with an invalid UUID {}".format(self.pool.pool.get_uuid_str()),
            True)

        # Restore th valid uuid to allow tearDown() to pass
        self.log.info(
            "Restoring the pool's valid uuid: %s", str(valid_uuid.value))
        self.pool.pool.uuid = valid_uuid

    @skipForTicket("DAOS-5545")
    def test_destroy_invalid_group(self):
        """Test destroying a valid pool but use the wrong server group.

        :avocado: tags=all,medium,full_regression
        :avocado: tags=pool,destroy,destroyinvalidgroup
        """
        hostlist_servers = self.hostlist_servers[:1]
        setid = self.params.get("setname", '/run/setnames/validsetname/')
        badsetid = self.params.get("setname", '/run/setnames/badsetname/')

        # Start servers
        self.start_servers({setid: hostlist_servers})

        # Create a pool
        self.validate_pool_creation(hostlist_servers, setid)

        # Change the pool server group name
        valid_group = self.pool.pool.group
        self.pool.pool.group = ctypes.create_string_buffer(badsetid)

        # Attempt to destroy the pool with an invalid server group name
        self.validate_pool_destroy(
            hostlist_servers,
            "with an invalid server group name {}".format(badsetid),
            True)

        # Restore the valid pool server group name to allow tearDown() to pass
        self.log.info(
            "Restoring the pool's valid server group name: %s",
            str(valid_group.value))
        self.pool.pool.group = valid_group

    @skipForTicket("DAOS-2742")
    def test_destroy_wrong_group(self):
        """Test destroying a pool.

         Destroy a pool on group A that was created on server group B,
         should fail.

        :avocado: tags=all,medium,full_regression
        :avocado: tags=pool,destroy,destroywronggroup
        """
        group_names = [self.server_group + "_a", self.server_group + "_b"]
        group_hosts = {
            group_names[0]: self.hostlist_servers[:1],
            group_names[1]: self.hostlist_servers[1:2],
        }
        self.start_servers(group_hosts)

        self.log.info("Create a pool in server group %s", group_names[0])
        self.add_pool(create=False)
        self.pool.name.value = group_names[0]
        self.pool.create()
        self.log.info("Pool UUID is %s", self.pool.uuid)

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
        self.pool.pool.group = ctypes.create_string_buffer(group_names[1])
        self.validate_pool_destroy(
            group_hosts[group_names[0]],
            "{} from the wrong server group {}".format(
                self.pool.uuid, group_names[1]),
            True)

        # Attempt to delete the pool from the right server group - should pass
        self.pool.pool.group = ctypes.create_string_buffer(group_names[0])
        self.validate_pool_destroy(
            group_hosts[group_names[1]],
            "{} from the right server group {}".format(
                self.pool.uuid, group_names[0]),
            False)

    def test_destroy_connected(self):
        """Destroy pool with connected client.

        Test destroying a pool that has a connected client with force == false.
        Should fail.

        :avocado: tags=all,medium,pr,daily_regression
        :avocado: tags=pool,destroy,destroyconnected
        """
        hostlist_servers = self.hostlist_servers[:1]

        # Start servers
        self.start_servers({self.server_group: hostlist_servers})

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

        :avocado: tags=all,medium,pr,daily_regression
        :avocado: tags=pool,destroy,forcedestroyconnected
        """
        hostlist_servers = self.hostlist_servers[:1]

        # Start servers
        self.start_servers({self.server_group: hostlist_servers})

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

        :avocado: tags=all,medium,pr,daily_regression
        :avocado: tags=pool,destroy,destroywithdata
        """
        hostlist_servers = self.hostlist_servers[:1]

        # Start servers
        self.start_servers({self.server_group: hostlist_servers})

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

    @skipForTicket("DAOS-2742")
    def test_destroy_async(self):
        """Destroy pool asynchronously.

        Create two server groups. Perform destroy asynchronously
        Expect the destroy to work on the server group where the pool was
        created and expect the destroy pool to fail on the second server.

        :avocado: tags=all,medium,full_regression
        :avocado: tags=pool,destroy,destroyasync
        """
        # Start two server groups
        group_names = [self.server_group + "_a", self.server_group + "_b"]
        group_hosts = {
            group_names[0]: self.hostlist_servers[:1],
            group_names[1]: self.hostlist_servers[1:2]
        }
        self.start_servers(group_hosts)

        self.add_pool(create=False)
        self.pool.name.value = group_names[0]
        self.pool.create()
        self.log.info("Pool UUID is %s on server_group %s",
                      self.pool.uuid, group_names[0])

        # Commented out due to DAOS-3836.
        # # Check that the pool was created on server_group_a
        # self.assertTrue(
        #    self.pool.check_files(group_hosts[group_names[0]]),
        #    "Pool data not detected on servers before destroy")

        # Commented out due to DAOS-3836.
        # # Check that the pool was not created on server_group_b
        # self.assertFalse(
        #    self.pool.check_files(group_hosts[group_names[1]]),
        #    "Pool data detected on servers before destroy")

        # Create callback handler
        cb_handler = CallbackHandler()

        # Destroy pool on server_group_a with callback
        self.log.info("Attempting to destroy pool")
        self.pool.pool.destroy(0, cb_handler.callback)
        cb_handler.wait()
        if cb_handler.ret_code != 0:
            self.fail("destroy-pool was expected to PASS")

        # Commented out due to DAOS-3836.
        # self.assertFalse(
        #    self.pool.check_files(group_hosts[group_names[0]]),
        #    "Pool data detected on {} after destroy".format(group_names[0]))

        # Destroy pool with callback while stopping other server
        # Create new pool on server_group_a
        self.add_pool(create=False)
        self.pool.name.value = group_names[0]
        self.pool.create()
        self.log.info("Pool UUID is %s on server_group %s",
                      self.pool.uuid, group_names[0])

        # Commented out due to DAOS-3836.
        # # Check that the pool was created on server_group_a
        # self.assertTrue(
        #    self.pool.check_files(group_hosts[group_names[0]]),
        #    "Pool data not detected on servers before destroy")

        # Commented out due to DAOS-3836.
        # # Check that the pool was not created on server_group_b
        # self.assertFalse(
        #    self.pool.check_files(group_hosts[group_names[1]]),
        #    "Pool data detected on servers before destroy")

        self.log.info("Stopping one server")
        try:
            self.server_managers[1].stop()
        except ServerFailed as error:
            self.fail(
                "Error stopping daos server group '{}': {}".format(
                    group_names[1], error))

        self.log.info("Attempting to destroy pool")
        self.pool.pool.destroy(0, cb_handler.callback)
        cb_handler.wait()
        if cb_handler.ret_code != 0:
            # Avoid attempting to destroy the pool again in tearDown()
            self.pool.pool = None
            self.fail("destroy-pool was expected to PASS")

        # Commented out due to DAOS-3836.
        # self.assertFalse(
        #    self.pool.check_files(group_hosts[group_names[1]]),
        #    "Pool data detected on servers after destroy")
