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

import os
import traceback
import server_utils
from avocado.utils import process
from apricot import TestWithServers, skipForTicket
from general_utils import get_pool, get_container
from general_utils import check_pool_files, TestPool, CallbackHandler
from daos_api import DaosApiError


class DestroyTests(TestWithServers):
    """Tests DAOS pool removal.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for destroy."""
        self.setup_start_servers_and_clients = False
        super(DestroyTests, self).setUp()

    def test_simple_delete(self):
        """Test destroying a pool created on a single server.

        :avocado: tags=pool,pooldestroy,quick
        """
        hostlist_servers = self.hostlist_servers[:1]
        group_hosts = {self.server_group: hostlist_servers}

        self.start_servers_and_clients(group_hosts)
        setid = self.params.get("setname", '/run/setnames/validsetname/')
        # use the uid/gid of the user running the test, these should
        # be perfectly valid
        uid = os.geteuid()
        gid = os.getegid()

        # Create pool with doasctl
        create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'
                      .format(self.daosctl, '0731', uid, gid, setid))

        uuid_str = """{0}""".format(process.system_output(create_cmd))
        print("uuid is {0}\n".format(uuid_str))
        # Check if pool exists
        self.assertTrue(
            check_pool_files(
                self.log, hostlist_servers, uuid_str.lower()),
            "ERROR: Pool data not detected on servers after create")
        self.log.info("Pool %s was successfully created", uuid_str)
        # delete pool
        try:
            delete_cmd = ('{0} destroy-pool -i {1} -s {2}'
                          .format(self.daosctl, uuid_str, setid))
            process.system(delete_cmd)
        except Exception as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("destroy-pool has failed with excep = {}".format(excep))
        # Verify that pool was deleted
        self.assertFalse(
            check_pool_files(
                self.log, hostlist_servers, uuid_str.lower()),
            "ERROR: Pool data detected on servers after destroy")
        self.log.info("Pool %s was successfully destroyed", uuid_str)

    @skipForTicket("DAOS-2739")
    def test_delete_doesnt_exist(self):
        """
        Test destroying a pool uuid that doesn't exist.

        :avocado: tags=pool,pooldestroy
        """
        hostlist_servers = self.hostlist_servers[:1]
        group_hosts = {self.server_group: hostlist_servers}

        self.start_servers_and_clients(group_hosts)

        setid = self.params.get("setname", '/run/setnames/validsetname/')
        # randomly selected uuid, that is exceptionally unlikely to exist
        bogus_uuid = '81ef94d7-a59d-4a5e-935b-abfbd12f2105'

        delete_cmd = ('{0} destroy-pool -i {1} -s {2}'.format(
            self.daosctl, bogus_uuid, setid))
        rc = process.system(delete_cmd)
        # if return code is successful; then destroy-pool has an issue
        print("rc = {}".format(rc))
        if rc == 0:
            self.log.info(
                "destroy-pool PASSED when destroying a non-existent pool")
            self.fail("daosctl destroy-pool was expected to fail but PASSED")
        self.assertFalse(
            check_pool_files(
                self.log, self.hostfile_servers, bogus_uuid.lower()),
            "ERROR: Pool {0} found when not expected.\n".format(bogus_uuid))
        self.log.info("Pool %s was not found, as expected ", bogus_uuid)

    def test_delete_wrong_servers(self):
        """
        Test destroying a pool valid pool but use the wrong server group.

        :avocado: tags=pool,pooldestroy
        """
        hostlist_servers = self.hostlist_servers[:1]
        group_hosts = {self.server_group: hostlist_servers}

        self.start_servers_and_clients(group_hosts)

        # need both a good and bad set
        goodsetid = self.params.get("setname", '/run/setnames/validsetname/')
        badsetid = self.params.get("setname", '/run/setnames/badsetname/')

        uid = os.geteuid()
        gid = os.getegid()

        # Create pool with doasctl
        create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'
                      .format(self.daosctl, '0731', uid, gid, goodsetid))
        uuid_str = """{0}""".format(process.system_output(create_cmd))
        print("uuid is {0}\n".format(uuid_str))
        # Check if pool exists
        self.assertTrue(
            check_pool_files(
                self.log, hostlist_servers, uuid_str.lower()),
            "ERROR: Pool data not detected on servers after create")
        self.log.info("Pool %s with %s was successfully created",
                      uuid_str, goodsetid)
        # delete pool
        try:
            delete_cmd = ('{0} destroy-pool -i {1} -s {2}'
                          .format(self.daosctl, uuid_str, badsetid))
            rc = process.system(delete_cmd)
            if rc == 0:
                self.log.info(
                    "destroy-pool destroyed the wrong group: %s", badsetid)
                self.fail(
                    "destroy-pool was expected to fail but PASSED")
        except process.CmdError as _:
            self.log.info(
                "destroy-pool failed as expected with group_name = {}".format(
                    badsetid))
            pass
        # Verify that pool was NOT deleted
        self.assertTrue(
            check_pool_files(
                self.log, hostlist_servers, uuid_str.lower()),
            "ERROR: Pool {} was deleted with {} group name after destroy"
            .format(uuid_str, badsetid))
        self.log.info(
            "Pool %s was not destroyed with group name %s", uuid_str, badsetid)
        # Cleanup remaining pool
        delete_cmd = ('{0} destroy-pool -i {1} -s {2}'
                      .format(self.daosctl, uuid_str, goodsetid))
        rc = process.system(delete_cmd)
        # Verify that pool was deleted
        self.assertFalse(
            check_pool_files(
                self.log, hostlist_servers, uuid_str.lower()),
            "ERROR: Pool {} was not deleted with {} group name after destroy"
            .format(uuid_str, goodsetid))
        self.log.info(
            "Pool %s was destroyed with group name %s", uuid_str, goodsetid)

    def test_multi_server_delete(self):
        """
        Test destroying a pool created on two servers.

        :avocado: tags=pool,pooldestroy,multiserver
        """
        hostlist_servers = self.hostlist_servers[:2]
        group_hosts = {self.server_group: hostlist_servers}

        self.start_servers_and_clients(group_hosts)

        setid = self.params.get("setname", '/run/setnames/validsetname/')
        # use the uid/gid of the user running the test, these should
        # be perfectly valid
        uid = os.geteuid()
        gid = os.getegid()

        # Create pool with doasctl
        create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'
                      .format(self.daosctl, '0731', uid, gid, setid))

        uuid_str = """{0}""".format(process.system_output(create_cmd))
        print("uuid is {0}\n".format(uuid_str))
        # Check if pool exists
        self.assertTrue(
            check_pool_files(
                self.log, hostlist_servers, uuid_str.lower()),
            "ERROR: Pool data not detected on servers after create")
        self.log.info("Pool %s was successfully created", uuid_str)
        # delete pool
        try:
            delete_cmd = ('{0} destroy-pool -i {1} -s {2}'
                          .format(self.daosctl, uuid_str, setid))
            process.system(delete_cmd)
        except Exception as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("destroy-pool has failed with excep = {}".format(excep))
        # Verify that pool was deleted
        self.assertFalse(
            check_pool_files(
                self.log, hostlist_servers, uuid_str.lower()),
            "ERROR: Pool data detected on servers after destroy")
        self.log.info("Pool %s was successfully destroyed", uuid_str)

    @skipForTicket("DAOS-2742")
    def test_bad_server_group(self):
        """Destroy a pool not owned by server group.

        Test destroying a pool created on server group A by passing
        in server group B, should fail.
        :avocado: tags=pool,pooldestroy
        """
        group_names = [self.server_group + "_a", self.server_group + "_b"]
        group_hosts = {
            group_names[0]: self.hostlist_servers[0:2],
            group_names[1]: self.hostlist_servers[2:4],
        }
        self.start_servers_and_clients(group_hosts)

        self.log.info("Create a pool in server group %s", group_names[0])
        cmd = "{} create-pool -m 0731 -u {} -g {} -s {}".format(
            self.daosctl, os.geteuid(), os.getegid(), group_names[0])
        pool_uuid = process.system_output(cmd)
        self.log.info("Pool UUID is %s", pool_uuid)

        self.assertTrue(
            check_pool_files(
                self.log, group_hosts[group_names[0]], pool_uuid.lower()),
            "Pool UUID {} not dected in server group {}".format(
                pool_uuid, group_names[0]))
        self.assertFalse(
            check_pool_files(
                self.log, group_hosts[group_names[0]], pool_uuid.lower()),
            "Pool UUID {} detected in server group {}".format(
                pool_uuid, group_names[1]))

        # Attempt to delete the pool from the wrong server group - should fail
        self.log.info(
            "Attempting to destroy pool %s from the wrong server group %s",
            pool_uuid, group_names[1])
        cmd = "{} destroy-pool -i {} -s {}".format(
            self.daosctl, pool_uuid, group_names[1])
        result = process.run(cmd, 30, True, True)
        self.assertTrue(
            result.exit_status != 0,
            "Able to delete the pool from the wrong server group")
        self.assertTrue(
            check_pool_files(
                self.log, group_hosts[group_names[0]], pool_uuid.lower()),
            "Pool UUID {} removed from server group {}".format(
                pool_uuid, group_names[0]))

        # Attempt to delete the pool from the right server group - should pass
        self.log.info(
            "Attempting to destroy pool %s from the right server group %s",
            pool_uuid, group_names[0])
        cmd = "{} destroy-pool -i {} -s {}".format(
            self.daosctl, pool_uuid, group_names[0])
        result = process.run(cmd, 30, True, True)
        self.assertTrue(
            result.exit_status == 0,
            "Unable to delete pool {} from the right server group {}".format(
                pool_uuid, group_names[0]))
        self.assertFalse(
            check_pool_files(
                self.log, group_hosts[group_names[0]], pool_uuid.lower()),
            "Pool UUID {} not removed from the right server group {}".format(
                pool_uuid, group_names[0]))

    @skipForTicket("DAOS-2742")
    def test_bad_server_group_api(self):
        """Test destroying a pool.

         Destroy a pool on group A that was created on server group B,
         should fail.
        :avocado: tags=pool,pooldestroy,pahender,api
        """
        group_names = [self.server_group + "_a", self.server_group + "_b"]
        group_hosts = {
            group_names[0]: self.hostlist_servers[0:2],
            group_names[1]: self.hostlist_servers[2:4],
        }
        self.start_servers_and_clients(group_hosts)

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
        self.pool.pool.group = group_names[1]
        self.log.info(
            "Attempting to destroy pool %s from the wrong server group %s",
            self.pool.uuid, self.pool.pool.group)

        self.log.info(
            "TestPool before destroy: pool=%s, uuid=%s", self.pool.pool,
            self.pool.uuid)

        try:
            self.pool.destroy(0)
        except Exception as error:
            self.log.error("Caught %s", error)

        self.log.info(
            "TestPool after destroy: pool=%s, uuid=%s", self.pool.pool,
            self.pool.uuid)

        self.assertIsNotNone(
            self.pool.pool,
            "Pool UUID {} was deleted with the wrong server group".format(uuid)
        )
        self.assertTrue(
            self.pool.check_files(group_hosts[group_names[0]]),
            "Pool UUID {} removed from server group {}".format(
                self.pool.uuid, group_names[0]))

        # Attempt to delete the pool from the right server group - should pass
        self.pool.pool.group = group_names[0]
        self.log.info(
            "Attempting to destroy pool %s from the right server group %s",
            self.pool.uuid, self.pool.pool.group)
        try:
            self.pool.destroy(0)
        except Exception as error:
            self.log.error("Caught %s", error)
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

        self.start_servers_and_clients(group_hosts)

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
        except Exception as error:
            self.log.error("Caught %s", error)

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

        self.start_servers_and_clients(group_hosts)
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
            except Exception as error:
                self.log.error("Caught %s", error)

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

        self.start_servers_and_clients(group_hosts)
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
            except Exception as error:
                self.log.error("Caught %s", error)

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

        self.start_servers_and_clients(group_hosts)

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

        try:
            # destroy pool with connection open
            self.log.info(
                "Attempting to destroy a connected pool %s",
                self.pool.get_uuid_str())
            self.pool.destroy(0)

            self.assertFalse(
                check_pool_files(
                    self.log, hostlist_servers, pool_uuid.lower()),
                "Pool data detected on servers after destroy")

            # should throw an exception and not hit this
            self.fail("Shouldn't hit this line.\n")

        except DaosApiError as excep:
            print("got exception which is expected so long as it is BUSY")
            print(excep)
            # print(traceback.format_exc())
            # pool should still be there

            self.assertTrue(
                check_pool_files(
                    self.log, hostlist_servers, pool_uuid.lower()),
                "Pool data not detected on servers after failed destroy")

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
        self.start_servers_and_clients(group_hosts)

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
        self.cb_handler = CallbackHandler()

        # Destroy pool on server_group_a with callback

        self.log.info("Attempting to destroy pool")
        self.pool.pool.destroy(0, self.cb_handler.callback)
        self.cb_handler.wait()
        if self.cb_handler.rc != 0:
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
        self.pool.pool.destroy(0, self.cb_handler.callback)
        self.cb_handler.wait()
        if self.cb_handler.rc != 0:
            self.fail("destroy-pool was expected to PASS")

        self.assertFalse(
            self.pool.check_files(group_hosts[group_names[1]]),
            "Pool data detected on servers after destroy")
