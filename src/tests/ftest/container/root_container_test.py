#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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

from command_utils import CommandFailure
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from dfuse_utils import Dfuse
from apricot import TestWithServers
from general_utils import pcmd
from ClusterShell.NodeSet import NodeSet

class RootContainerTest(TestWithServers):
    """Base Dfuse Container check test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a RootContainerTest object."""
        super(RootContainerTest, self).__init__(*args, **kwargs)
        self.pool = []
        self.container = []
        self.tmp_file_count = self.params.get(
            "tmp_file_count", '/run/container/*')
        self.cont_count = self.params.get(
            "cont_count", '/run/container/*')
        self.tmp_file_size = self.params.get(
            "tmp_file_size", '/run/container/*')
        self.tmp_file_name = self.params.get(
            "tmp_file_name", '/run/container/*')
        # device where the pools and containers are created
        self.device = "scm"

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(RootContainerTest, self).setUp()
        self.dfuse = None
        self.dfuse_hosts = None

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.dfuse:
                self.dfuse.stop()
        finally:
            # Stop the servers and agents
            super(RootContainerTest, self).tearDown()

    def _create_pool(self):
        """Create a TestPool object to use with ior.
        """
        # Get the pool params
        pool = TestPool(
            self.context, dmg_command=self.get_dmg_command())
        pool.get_params(self)
        # Create a pool
        pool.create()
        self.pool.append(pool)
        return pool

    def _create_cont(self, pool, path=None):
        """Create a TestContainer object to be used to create container.

           Args:
               pool (TestPool): pool object
               path (str): Unified namespace path for container
        """
        # Get container params
        container = TestContainer(pool, daos_command=DaosCommand(self.bin))
        container.get_params(self)
        if path is not None:
            container.path.update(path)
        # create container
        container.create()
        self.container.append(container)
        return container

    def _start_dfuse(self, pool, container):
        """Create a DfuseCommand object to start dfuse.

           Args:
               container: Container to mount dfuse
        """

        # Get Dfuse params
        self.dfuse = Dfuse(self.dfuse_hosts, self.tmp)
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(pool)
        self.dfuse.set_dfuse_cont_param(container)
        self.dfuse.set_dfuse_exports(self.server_managers[0], self.client_log)

        try:
            # start dfuse
            self.dfuse.run()
        except CommandFailure as error:
            self.log.error("Dfuse command %s failed on hosts %s",
                           str(self.dfuse),
                           self.dfuse.hosts,
                           exc_info=error)
            self.fail("Test was expected to pass but it failed.\n")

    def test_rootcontainer(self):
        """Jira ID: DAOS-3782.

        Test Description:
            Purpose of this test is to try and create a container and
            mount it over dfuse and use it as a root container and create
            subcontainers underneath it and insert several files and see
            if they can be accessed using ls and cd. Verify the pool size
            reflects the space occupied by container. Try to remove the
            files and containers and see the space is reclaimed.
            Test the above procedure with 100 sub containers.
            Test the above procedure with 5 pools and 50 containers
            spread across the pools.
        :avocado: tags=all,hw,small,full_regression,container
        :avocado: tags=rootcontainer
        """

        # Create a pool and start dfuse.
        pool = self._create_pool()
        container = self._create_cont(pool)
        self.dfuse_hosts = self.agent_managers[0].hosts
        # mount fuse
        self._start_dfuse(pool, container)
        # Create another container and add it as sub container under
        # root container
        sub_container = str(self.dfuse.mount_dir.value + "/cont0")
        container = self._create_cont(pool, path=sub_container)
        #Insert files into root container
        self.insert_files_and_verify("")
        #Insert files into sub container
        self.insert_files_and_verify("cont0")
        #Create 100 subcontainer and verify the temp files
        self.verify_create_delete_containers(pool, 100)
        self.verify_multi_pool_containers()

    def verify_multi_pool_containers(self):
        """Create several pools and containers and mount it
           under the root container and verify they're
           accessible.
        """
        pool_count = self.params.get("pool_count", "/run/pool/*")
        for i in range(pool_count):
            pool = self._create_pool()
            for j in range(self.cont_count):
                cont_name = "/cont_{}{}".format(i, j)
                sub_cont = str(self.dfuse.mount_dir.value + cont_name)
                self._create_cont(pool=pool, path=sub_cont)
                self.insert_files_and_verify(cont_name)

    def verify_create_delete_containers(self, pool, cont_count):
        """Create multiple containers and multiple multi-mb files
           in each of them and verify the space usage.
           Destroy half of the containers and verify the space
           usage is reclaimed.

           Args:
               cont_count (int): Number of containers to be created.
        """
        self.log.info("Verifying multiple container create delete")
        pool_space_before = pool.get_pool_free_space(self.device)
        self.log.info("Pool space before = %s", pool_space_before)
        for i in range(cont_count):
            sub_cont = str(self.dfuse.mount_dir.value + "/cont{}".format(i+1))
            self._create_cont(pool, path=sub_cont)
            self.insert_files_and_verify("cont{}".format(i+1))
        expected = pool_space_before - \
                   cont_count * self.tmp_file_count * self.tmp_file_size
        pool_space_after = pool.get_pool_free_space(self.device)
        self.log.info("Pool space <= Expected")
        self.log.info("%s <= %s", pool_space_after, expected)
        self.assertTrue(pool_space_after <= expected)
        self.log.info("Destroying half of the containers = %s",
                      cont_count//2)
        for i in range(cont_count // 2):
            self.container[-1].destroy(1)
            self.container.pop()
        expected = pool_space_after + \
                   ((cont_count // 2) * self.tmp_file_count *\
                    self.tmp_file_size)
        pool_space_after_cont_destroy = \
                   pool.get_pool_free_space(self.device)
        self.log.info("After container destroy")
        self.log.info("Free Pool space >= Expected")
        self.log.info("%s >= %s", pool_space_after_cont_destroy, expected)
        self.assertTrue(pool_space_after_cont_destroy >= expected)

    def insert_files_and_verify(self, container_name):
        """ Insert files into the specific container and verify
            they're navigable and accessible.

        Args:
            container_name: Name of the POSIX Container
            file_name_prefix: Prefix of the file name that will be created
            no_of_files: Number of files to be created iteratively

        Return:
            None
        """
        cont_dir = self.dfuse.mount_dir.value
        if container_name:
            cont_dir = "{}/{}".format(cont_dir, container_name)

        cmds = []
        ls_cmds = []

        for i in range(self.tmp_file_count):
            # Create 40 MB files
            file_name = "{}{}".format(self.tmp_file_name, i+1)
            cmd = "head -c {} /dev/urandom > {}/{}".format(
                self.tmp_file_size, cont_dir, file_name)
            ls_cmds.append("ls {}".format(file_name))
            cmds.append(cmd)
        self._execute_cmd(";".join(cmds))

        cmds = []
        # Run ls to verify the temp files are actually created
        cmds = ["cd {}".format(cont_dir)]
        cmds.extend(ls_cmds)
        self._execute_cmd(";".join(cmds))

    def _execute_cmd(self, cmd):
        """Execute command on the host clients

           Args:
               cmd (str): Command to run
        """

        try:
            # execute bash cmds
            ret = pcmd(
                self.dfuse_hosts, cmd, verbose=True, timeout=30)
            if 0 not in ret:
                error_hosts = NodeSet(
                    ",".join(
                        [str(node_set) for code, node_set in
                         ret.items() if code != 0]))
                raise CommandFailure(
                    "Error running '{}' on the following "
                    "hosts: {}".format(cmd, error_hosts))

         # report error if any command fails
        except CommandFailure as error:
            self.log.error("DfuseSparseFile Test Failed: %s",
                           str(error))
            self.fail("Test was expected to pass but "
                      "it failed.\n")
        return ret
