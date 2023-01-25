"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from dfuse_test_base import DfuseTestBase
from run_utils import run_remote


class RootContainerTest(DfuseTestBase):
    """Base Dfuse Container check test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a RootContainerTest object."""
        super().__init__(*args, **kwargs)
        self.pool = []
        self.container = []
        self.tmp_file_count = self.params.get("tmp_file_count", '/run/container/*')
        self.cont_count = self.params.get("cont_count", '/run/container/*')
        self.tmp_file_size = self.params.get("tmp_file_size", '/run/container/*')
        self.tmp_file_name = self.params.get("tmp_file_name", '/run/container/*')
        # device where the pools and containers are created
        self.device = "scm"

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()
        self.dfuse_hosts = None

    def _create_pool(self):
        """Add a new TestPool object to the list of pools.

        Returns:
            TestPool: the newly added pool

        """
        self.pool.append(self.get_pool(connect=False))
        return self.pool[-1]

    def _create_cont(self, pool, **params):
        """Add a new TestContainer object to the list of containers.

        Args:
            pool (TestPool): pool object
            params (dict, optional): name/value of container attributes to update

        Returns:
            TestContainer: the newly added container

        """
        container = self.get_container(pool, **params)
        self.container.append(container)
        return container

    def test_dfuse_root_container(self):
        """Jira ID: DAOS-3782.

        Test Description:
            Purpose of this test is to try and create a container and
            mount it over dfuse and use it as a root container and create
            sub containers underneath it and insert several files and see
            if they can be accessed using ls and cd. Verify the pool size
            reflects the space occupied by container. Try to remove the
            files and containers and see the space is reclaimed.
            Test the above procedure with 100 sub containers.
            Test the above procedure with 5 pools and 50 containers
            spread across the pools.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container,dfuse
        :avocado: tags=RootContainerTest,test_dfuse_root_container
        """
        # Create a pool and start dfuse.
        pool = self._create_pool()
        container = self._create_cont(pool)
        self.dfuse_hosts = self.agent_managers[0].hosts
        # mount fuse
        self.start_dfuse(self.dfuse_hosts, pool, container)
        # Create another container and add it as sub container under
        # root container
        sub_container = str(self.dfuse.mount_dir.value + "/cont0")
        container = self._create_cont(pool, path=sub_container)
        # Insert files into root container
        self.insert_files_and_verify("")
        # Insert files into sub container
        self.insert_files_and_verify("cont0")
        # Create 100 sub containers and verify the temp files
        self.verify_create_delete_containers(pool, 100)
        self.verify_multi_pool_containers()

    def verify_multi_pool_containers(self):
        """Verify multiple pools and containers.

        Create several pools and containers and mount it under the root
        container and verify they're accessible.
        """
        pool_count = self.params.get("pool_count", "/run/pool/*")
        for idx in range(pool_count):
            pool = self._create_pool()
            for jdx in range(self.cont_count):
                cont_name = "/cont_{}{}".format(idx, jdx)
                sub_cont = str(self.dfuse.mount_dir.value + cont_name)
                self._create_cont(pool=pool, path=sub_cont)
                self.insert_files_and_verify(cont_name)

    def verify_create_delete_containers(self, pool, cont_count):
        """Verify multiple pools and containers creation and deletion.

        Create multiple containers and multiple multi-mb files in each of
        them and verify the space usage.

        Destroy half of the containers and verify the space usage is reclaimed.

        Args:
            cont_count (int): Number of containers to be created.
        """
        self.log.info("Verifying multiple container create delete")
        pool_space_before = pool.get_pool_free_space(self.device)
        self.log.info("Pool space before = %s", pool_space_before)
        for idx in range(cont_count):
            sub_cont = str(self.dfuse.mount_dir.value + "/cont{}".format(idx + 1))
            self._create_cont(pool, path=sub_cont)
            self.insert_files_and_verify("cont{}".format(idx + 1))
        expected = pool_space_before - \
            cont_count * self.tmp_file_count * self.tmp_file_size
        pool_space_after = pool.get_pool_free_space(self.device)
        self.log.info("Pool space <= Expected")
        self.log.info("%s <= %s", pool_space_after, expected)
        self.assertTrue(pool_space_after <= expected)
        self.log.info("Destroying half of the containers = %s", cont_count // 2)
        for _ in range(cont_count // 2):
            self.container[-1].destroy(1)
            self.container.pop()
        expected = pool_space_after + \
            ((cont_count // 2) * self.tmp_file_count * self.tmp_file_size)
        pool_space_after_cont_destroy = \
            pool.get_pool_free_space(self.device)
        self.log.info("After container destroy")
        self.log.info("Free Pool space >= Expected")
        self.log.info("%s >= %s", pool_space_after_cont_destroy, expected)
        self.assertTrue(pool_space_after_cont_destroy >= expected)

    def insert_files_and_verify(self, container_name):
        """Verify inserting files into a specific container.

        Insert files into the specific container and verify they're navigable
        and accessible.

        Args:
            container_name: Name of the POSIX Container
            file_name_prefix: Prefix of the file name that will be created
            no_of_files: Number of files to be created iteratively
        """
        cont_dir = self.dfuse.mount_dir.value
        if container_name:
            cont_dir = "{}/{}".format(cont_dir, container_name)

        cmds = []
        ls_cmds = []

        for idx in range(self.tmp_file_count):
            # Create 40 MB files
            file_name = "{}{}".format(self.tmp_file_name, idx + 1)
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
        """Execute command on the host clients.

        Args:
            cmd (str): Command to run

        """
        result = run_remote(self.log, self.dfuse_hosts, cmd, timeout=30)
        if not result.passed:
            self.log.error(
                "Error running '%s' on the following hosts: %s", cmd, result.failed_hosts)
            self.fail("Test was expected to pass but it failed.\n")
