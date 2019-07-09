#!/usr/bin/python2
"""
(C) Copyright 2018-2019 Intel Corporation.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
The Government's rights to use, modify, reproduce, release, perform, display,
or disclose this software are subject to the terms of the Apache License as
provided in Contract No. B609815.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

import server_utils
from apricot import TestWithServers, skipForTicket
from general_utils import get_pool, get_container
from general_utils import check_pool_files, TestPool, CallbackHandler
from daos_api import DaosApiError
import ctypes


class DestroyTests(TestWithServers):
    """Tests DAOS pool removal.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for destroy."""
        self.setup_start_servers = False
        super(DestroyTests, self).setUp()

    def test_simple_delete(self):
        """Test destroying a pool created on a single server.

        :avocado: tags=pool,pooldestroy,quick
        """
        hostlist_servers = self.hostlist_servers[:1]
        setid = self.params.get("setname", '/run/setnames/validsetname/')
        group_hosts = {setid: hostlist_servers}

        # Start servers with server group
        self.start_servers(group_hosts)

        # Create a pool
        self.log.info("Create a pool")
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.name.value = setid
        self.pool.create()
        self.log.info("Pool UUID is %s", self.pool.uuid)

        # Check that the pool was created
        self.assertTrue(
            self.pool.check_files(hostlist_servers),
            "Pool data not detected on servers before destroy")

        # Destroy pool with direct API call (no disconnect)
        try:
            self.log.info("Attempting to destroy pool")
            self.pool.pool.destroy(0)
        except DaosApiError as result:
            self.log.info(
                "Detected exception while destroying a pool %s", str(result))

        self.log.info("Check if files still exist")
        self.assertFalse(
            self.pool.check_files(hostlist_servers),
            "Pool UUID {} was not destroyed".format(
                self.pool.uuid))

    @skipForTicket("DAOS-2739")
    def test_delete_doesnt_exist(self):
        """
        Test destroying a pool uuid that doesn't exist.

        :avocado: tags=pool,pooldestroy
        """
        hostlist_servers = self.hostlist_servers[:1]
        setid = self.params.get("setname", '/run/setnames/validsetname/')
        group_hosts = {setid: hostlist_servers}

        # Start servers in each server group
        self.start_servers(group_hosts)

        # Create a pool
        self.log.info("Create a pool")
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.name.value = setid
        self.pool.create()
        self.log.info("Valid Pool UUID is %s", self.pool.uuid)

        # Check that the pool was created
        self.assertTrue(
            self.pool.check_files(hostlist_servers),
            "Pool data not detected on servers before destroy")

        # Attempt to destroy pool with invald UUID
        saved_uuid = self.pool.uuid
        bogus_uuid = '81ef94d7-a59d-4a5e-935b-abfbd12f2105'
        self.pool.uuid = bogus_uuid
        self.log.info("Deleting pool with Invalid Pool UUID:  %s",
                      self.pool.uuid)
        try:
            self.log.info("Attempting to destroy pool with an invalid UUID")
            # call daos destroy api directly
            self.pool.pool.destroy(0)
        # exception is expected
        except DaosApiError as result:
            self.log.info(
                "Expected exception - destroying pool with invalid UUID\n %s",
                str(result))
            # restore the valid UUID and check if valid pool still exists
            self.pool.uuid = saved_uuid
            self.log.info("Check if valid files still exist")
            self.assertTrue(
                self.pool.check_files(hostlist_servers),
                "Valid pool data was not detected on servers after "
                "attempting to destroy an invalid pool")
            return
        # if here then pool-destroy did not raise an exception as expected
        # restore the valid UUID and check if valid pool still exists
        self.log.info(
            "DAOS api exception did not occur"
            " - destroyed pool with invalid UUID")
        self.pool.uuid = saved_uuid
        self.log.info("Check if valid files still exist")
        self.assertTrue(
            self.pool.check_files(hostlist_servers),
            "Valid pool data was not detected on servers after "
            "a destroy-pool with invalid UUID failed to raise an exception")
        self.fail(
            "Test did not raise an exception when "
            "deleting a pool with invalid UUID")

    def test_delete_wrong_servers(self):
        """
        Test destroying a valid pool but use the wrong server group.

        :avocado: tags=pool,pooldestroy
        """
        hostlist_servers = self.hostlist_servers[:1]
        setid = self.params.get("setname", '/run/setnames/validsetname/')
        badsetid = self.params.get("setname", '/run/setnames/badsetname/')

        group_hosts = {setid: hostlist_servers}
        # Start servers with valid group name
        self.start_servers(group_hosts)

        # Create a pool
        self.log.info("Create a pool")
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.name.value = setid
        self.pool.create()
        self.log.info("Pool created with server group %s",
                      self.pool.name.value)

        # Check that the pool was created
        self.assertTrue(
            self.pool.check_files(hostlist_servers),
            "Pool data not detected on servers before destroy")

        # Attempt to destroy pool with invald server group name

        self.pool.pool.group = ctypes.create_string_buffer(badsetid)
        self.log.info("Deleting pool with Invalid Server group name:  %s",
                      badsetid)
        try:
            self.log.info("Attempting to destroy pool from invalid group")
            # call DaosPool destroy api directly
            self.pool.pool.destroy(0)
        # exception is expected
        except DaosApiError as result:
            self.log.info(
                "Expected exception - destroying pool with invalid group %s",
                str(result))
            # restore the valid server group and check if valid pool
            # still exists
            self.pool.pool.group = ctypes.create_string_buffer(setid)
            self.log.info("Check if valid files still exist")
            self.assertTrue(
                self.pool.check_files(hostlist_servers),
                "Valid pool data was not detected on servers after "
                "attempting to destroy an invalid pool")
            return
        # if here then pool-destroy did not raise an exception as expected
        # restore the valid server group and check if valid pool still exists
        self.log.info(
            "DAOS api exception did not occur"
            " - destroyed pool with invalid server group")
        self.pool.pool.group = ctypes.create_string_buffer(setid)
        self.log.info("Check if valid files still exist")
        self.assertTrue(
            self.pool.check_files(hostlist_servers),
            "Valid pool data was not detected on servers after "
            "a destroy-pool with invalid server group failed to "
            "raise an exception")
        self.fail(
            "Test did not raise an exception when "
            "deleting a pool with invalid server group")

    def test_multi_server_delete(self):
        """
        Test destroying a pool created on two servers.

        :avocado: tags=pool,pooldestroy,multiserver
        """
        hostlist_servers = self.hostlist_servers[:2]
        setid = self.params.get("setname", '/run/setnames/validsetname/')
        group_hosts = {setid: hostlist_servers}

        # Start servers with server group
        self.start_servers(group_hosts)

        # Create a pool
        self.log.info("Create a pool")
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.name.value = setid
        self.pool.create()
        self.log.info("Pool UUID is %s", self.pool.uuid)

        # Check that the pool was created
        self.assertTrue(
            self.pool.check_files(hostlist_servers),
            "Pool data not detected on servers before destroy")

        # Destroy pool with direct API call (no disconnect)
        try:
            self.log.info("Attempting to destroy pool")
            self.pool.pool.destroy(0)
        except DaosApiError as result:
            self.log.info(
                "Detected exception while destroying a pool %s", str(result))

        self.log.info("Check if files still exist")
        self.assertFalse(
            self.pool.check_files(hostlist_servers),
            "Pool UUID {} was not destroyed".format(
                self.pool.uuid))

    @skipForTicket("DAOS-2742")
    def test_bad_server_group(self):
        """Test destroying a pool.

         Destroy a pool on group A that was created on server group B,
         should fail.
        :avocado: tags=pool,pooldestroy
        """
        group_names = [self.server_group + "_a", self.server_group + "_b"]
        group_hosts = {
            group_names[0]: self.hostlist_servers[:1],
            group_names[1]: self.hostlist_servers[1:2],
        }
        self.start_servers(group_hosts)

        self.log.info("Create a pool in server group %s", group_names[0])
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.name.value = group_names[0]
        self.pool.create()
        self.log.info("Pool UUID is %s", self.pool.uuid)
        uuid = self.pool.uuid

        self.assertTrue(
            self.pool.check_files(group_hosts[group_names[0]]),
            "Pool UUID {} not dected in server group {}".format(
                self.pool.uuid, group_names[0]))
        self.assertFalse(
            self.pool.check_files(group_hosts[group_names[1]]),
            "Pool UUID {} detected in server group {}".format(
                self.pool.uuid, group_names[1]))

        # Attempt to delete the pool from the wrong server group - should fail
        self.pool.pool.group = ctypes.create_string_buffer(group_names[1])
        self.log.info(
            "Attempting to destroy pool %s from the wrong server group %s",
            self.pool.uuid, group_names[1])

        self.log.info(
            "TestPool after destroy: pool=%s, uuid=%s, attached=%s",
            self.pool.pool, self.pool.uuid, self.pool.pool.attached)

        try:
            self.pool.pool.destroy(0)
        except DaosApiError as result:
            self.log.info(
                "Detected exception while destroying a pool %s", str(result))

        self.log.info(
            "TestPool after destroy: pool=%s, uuid=%s, attached=%s",
            self.pool.pool, self.pool.uuid, self.pool.pool.attached)

        self.assertIsNotNone(
            self.pool.pool,
            "Pool UUID {} was deleted with the wrong server group".format(uuid)
        )
        self.assertTrue(
            self.pool.check_files(group_hosts[group_names[0]]),
            "Pool UUID {} removed from server group {}".format(
                self.pool.uuid, group_names[0]))

        # Attempt to delete the pool from the right server group - should pass
        self.pool.pool.group = ctypes.create_string_buffer(group_names[0])
        self.log.info(
            "Attempting to destroy pool %s from the right server group %s",
            self.pool.uuid, group_names[0])
        try:
            self.pool.pool.destroy(0)
        except DaosApiError as result:
            self.fail("Detected exception while destroying a pool {}".format(
                str(result)))
        self.assertFalse(
            self.pool.check_files(group_hosts[group_names[0]]),
            "Pool UUID {} not removed from the right server group {}".format(
                self.pool.uuid, group_names[0]))

    @skipForTicket("DAOS-2741")
    def test_destroy_connect(self):
        """Destroy pool with connected client.

        Test destroying a pool that has a connected client with force == false.
        Should fail.
        :avocado: tags=pool,pooldestroy
        """
        hostlist_servers = self.hostlist_servers[:1]
        group_hosts = {self.server_group: hostlist_servers}

        self.start_servers(group_hosts)

        self.log.info("Create a pool")
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.name.value = self.server_group
        self.pool.create()
        self.log.info("Pool UUID is %s", self.pool.uuid)

        # Check that the pool was created
        self.assertTrue(
            self.pool.check_files(hostlist_servers),
            "Pool data not detected on servers before destroy")

        # Connect to the pool
        self.assertTrue(self.pool.connect(1),
                        "Pool connect failed before destroy")

        # Destroy pool with direct API call (no disconnect)
        try:
            self.log.info("Attempting to destroy pool")
            self.pool.pool.destroy(0)
        except DaosApiError as result:
            self.log.info(
                "Detected exception while destroying a pool %s", str(result))

        self.log.info("Check if files still exist")
        self.assertTrue(
            self.pool.check_files(hostlist_servers),
            "Pool UUID {} should not be removed when connected".format(
                self.pool.uuid))

    def test_destroy_recreate(self):
        """Destroy and recreate pool multiple times.

        Test destroy and recreate one right after the other multiple times
        Should fail.
        :avocado: tags=pool,pooldestroy,destroyredo
        """
        hostlist_servers = self.hostlist_servers[:1]
        group_hosts = {self.server_group: hostlist_servers}

        self.start_servers(group_hosts)
        counter = 0

        while counter < 10:
            self.pool = TestPool(self.context, self.log)
            self.pool.get_params(self)
            self.pool.name.value = self.server_group
            self.pool.create()
            self.log.info("Pool UUID is %s", self.pool.uuid)

            # Check that the pool was created
            self.assertTrue(
                self.pool.check_files(hostlist_servers),
                "Pool data not detected on servers before destroy")

            # Destroy pool with direct API call
            try:
                self.log.info("Attempting to destroy pool")
                self.pool.pool.destroy(0)
            except DaosApiError as result:
                self.fail(
                    "Detected exception while destroying a pool {}".format(
                        str(result)))
            self.log.info("Check if files still exist")
            self.assertFalse(
                self.pool.check_files(hostlist_servers),
                "Pool UUID {} still exists".format(
                    self.pool.uuid))
            counter += 1

    def test_many_servers(self):
        """
        Test destroy on a large (relative) number of servers.

        :avocado: tags=pool,pooldestroy,destroybig
        """
        counter = 0
        hostlist_servers = self.hostlist_servers[:6]
        group_hosts = {self.server_group: hostlist_servers}

        self.start_servers(group_hosts)
        counter = 0

        while counter < 10:
            self.pool = TestPool(self.context, self.log)
            self.pool.get_params(self)
            self.pool.name.value = self.server_group
            self.pool.create()
            self.log.info("Pool UUID is %s", self.pool.uuid)

            # Check that the pool was created
            self.assertTrue(
                self.pool.check_files(hostlist_servers),
                "Pool data not detected on servers before destroy")

            # Destroy pool with direct API call
            try:
                self.log.info("Attempting to destroy pool")
                self.pool.pool.destroy(0)
            except DaosApiError as result:
                self.fail("Caught {}".format(result))

            self.log.info("Check if files still exist")
            self.assertFalse(
                self.pool.check_files(hostlist_servers),
                "Pool UUID {} still exists".format(
                    self.pool.uuid))
            counter += 1

    @skipForTicket("DAOS-2741")
    def test_destroy_withdata(self):
        """Destroy Pool with data.

        Test destroy and recreate one right after the other multiple times
        Should fail.
        :avocado: tags=pool,pooldestroy,destroydata
        """
        hostlist_servers = self.hostlist_servers[:1]
        group_hosts = {self.server_group: hostlist_servers}

        self.start_servers(group_hosts)

        # parameters used in pool create
        createmode = self.params.get("mode", "/run/pool/*")
        createsetid = self.params.get("name", "/run/pool/*")
        createsize = self.params.get("scm_size", "/run/pool/*")

        # parameters used in pool create
        self.pool = get_pool(
            self.context, createmode, createsize, createsetid, log=self.log)
        pool_uuid = self.pool.get_uuid_str()
        self.log.info("Connected to pool %s", pool_uuid)

        self.container = get_container(self.context, self.pool, self.log)
        cont_uuid = self.container.get_uuid_str()

        self.log.info("Writing 4096 bytes to the container %s", cont_uuid)
        self.container.write_an_obj("123456789", 9, "DKEY", "AKEY", obj_cls=1)

        self.assertTrue(
            check_pool_files(
                self.log, hostlist_servers, pool_uuid.lower()),
            "Pool data not detected on servers before destroy")
        self.log.info("Attempting to destroy a connected pool %s",
                      self.pool.get_uuid_str())
        try:
            # destroy pool with connection open
            ret_code = self.pool.destroy(0)
        except DaosApiError as result:
            self.log.info(
                "Detected exception that was expected %s", str(result))
        if ret_code == 0:
            self.log.info(
                "destroy-pool destroyed a connected pool")
            self.fail(
                "destroy-pool was expected to fail but PASSED")
        self.log.info(
            "destroy-pool failed as expected ")

        self.assertTrue(
            check_pool_files(
                self.log, hostlist_servers, pool_uuid.lower()),
            "Pool data not detected on servers after destroy")

    @skipForTicket("DAOS-2742")
    def test_destroy_async(self):
        """Destroy pool asynchronously.

        Create two server groups. Perform destroy asynchronously
        Expect the destroy to work on the server group where the pool was
        created and expect the destroy pool to fail on the second server.
        :avocado: tags=pool,pooldestroy,destroyasync
        """
        # Start two server groups
        group_names = [self.server_group + "_a", self.server_group + "_b"]
        group_hosts = {
            group_names[0]: self.hostlist_servers[:1],
            group_names[1]: self.hostlist_servers[1:2]
        }
        self.start_servers(group_hosts)

        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.name.value = group_names[0]
        self.pool.create()
        self.log.info("Pool UUID is %s on server_group %s",
                      self.pool.uuid, group_names[0])

        # Check that the pool was created on server_group_a
        self.assertTrue(
            self.pool.check_files(group_hosts[group_names[0]]),
            "Pool data not detected on servers before destroy")

        # Check that the pool was not created on server_group_b
        self.assertFalse(
            self.pool.check_files(group_hosts[group_names[1]]),
            "Pool data detected on servers before destroy")

        # Create callback handler
        cb_handler = CallbackHandler()

        # Destroy pool on server_group_a with callback

        self.log.info("Attempting to destroy pool")
        self.pool.pool.destroy(0, cb_handler.callback)
        cb_handler.wait()
        if cb_handler.ret_code != 0:
            self.fail("destroy-pool was expected to PASS")

        self.assertFalse(
            self.pool.check_files(group_hosts[group_names[0]]),
            "Pool data detected on {} after destroy".format(group_names[0]))

        # Destroy pool with callback while stopping other server
        # Create new pool on server_group_a
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.name.value = group_names[0]
        self.pool.create()
        self.log.info("Pool UUID is %s on server_group %s",
                      self.pool.uuid, group_names[0])

        # Check that the pool was created on server_group_a
        self.assertTrue(
            self.pool.check_files(group_hosts[group_names[0]]),
            "Pool data not detected on servers before destroy")

        # Check that the pool was not created on server_group_b
        self.assertFalse(
            self.pool.check_files(group_hosts[group_names[1]]),
            "Pool data detected on servers before destroy")

        self.log.info("Stopping one server")
        server_utils.stop_server(hosts=group_hosts[group_names[1]])

        self.log.info("Attempting to destroy pool")
        self.pool.pool.destroy(0, cb_handler.callback)
        cb_handler.wait()
        if cb_handler.ret_code != 0:
            self.fail("destroy-pool was expected to PASS")

        self.assertFalse(
            self.pool.check_files(group_hosts[group_names[1]]),
            "Pool data detected on servers after destroy")
