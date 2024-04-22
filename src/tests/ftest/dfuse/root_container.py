"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from run_utils import run_remote


class RootContainerTest(TestWithServers):
    """Base Dfuse Container check test class.

    :avocado: recursive
    """

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
        tmp_file_count = self.params.get("tmp_file_count", '/run/container/*')
        cont_count = self.params.get("cont_count", '/run/container/*')
        tmp_file_size = self.params.get("tmp_file_size", '/run/container/*')
        tmp_file_name = self.params.get("tmp_file_name", '/run/container/*')
        device = "scm"

        # Create a pool and start dfuse.
        pool = self.get_pool(connect=False)
        container = self.get_container(pool)
        dfuse_hosts = self.agent_managers[0].hosts
        # mount fuse
        dfuse = get_dfuse(self, dfuse_hosts)
        start_dfuse(self, dfuse, pool, container)
        # Create another container and add it as sub container under the root container
        sub_container = str(dfuse.mount_dir.value + "/cont0")
        container = self.get_container(pool, path=sub_container)
        # Insert files into root container
        self.insert_files_and_verify(
            dfuse_hosts, dfuse.mount_dir.value, tmp_file_count, tmp_file_name, tmp_file_size)
        # Insert files into sub container
        self.insert_files_and_verify(
            dfuse_hosts, os.path.join(dfuse.mount_dir.value, 'cont0'), tmp_file_count,
            tmp_file_name, tmp_file_size)
        # Create 100 sub containers and verify the temp files
        self.verify_create_delete_containers(
            pool, device, 100, dfuse_hosts, dfuse.mount_dir.value, tmp_file_count, tmp_file_size,
            tmp_file_size)
        self.verify_multi_pool_containers(
            cont_count, dfuse_hosts, dfuse.mount_dir.value, tmp_file_count, tmp_file_name,
            tmp_file_size)

    def verify_multi_pool_containers(self, cont_count, hosts, mount_dir, tmp_file_count,
                                     tmp_file_name, tmp_file_size):
        """Verify multiple pools and containers.

        Create several pools and containers and mount it under the root
        container and verify they're accessible.

        Args:
            cont_count (int): number of containers to create
            hosts (NodeSet): Hosts on which to run the commands
            mount_dir (str): dfuse mount directory
            tmp_file_count (int): number of temporary files
            tmp_file_name (str): base name for temporary files
            tmp_file_size (int): size of temporary files
        """
        pool_count = self.params.get("pool_count", "/run/pool/*")
        for idx in range(pool_count):
            pool = self.get_pool(connect=False)
            for jdx in range(cont_count):
                cont_name = f"/cont_{idx}{jdx}"
                sub_cont = str(mount_dir.value + cont_name)
                self.get_container(pool=pool, path=sub_cont)
                self.insert_files_and_verify(
                    hosts, os.path.join(mount_dir, cont_name), tmp_file_count, tmp_file_name,
                    tmp_file_size)

    def verify_create_delete_containers(self, pool, device, cont_count, hosts, mount_dir,
                                        tmp_file_count, tmp_file_name, tmp_file_size):
        """Verify multiple pools and containers creation and deletion.

        Create multiple containers and multiple multi-mb files in each of
        them and verify the space usage.

        Destroy half of the containers and verify the space usage is reclaimed.

        Args:
            pool (TestPool): pool in which to create the containers
            device (str): device where the pools and containers are created
            cont_count (int): Number of containers to be created.
            hosts (NodeSet): Hosts on which to run the commands
            tmp_file_count (int): number of temporary files
            tmp_file_name (str): base name for temporary files
            tmp_file_size (int): size of temporary files
        """
        self.log.info("Verifying multiple container create delete")
        pool_space_before = pool.get_pool_free_space(device)
        self.log.info("Pool space before = %s", pool_space_before)
        containers = []
        for idx in range(cont_count):
            sub_cont = str(mount_dir + f"/cont{idx + 1}")
            containers.append(self.get_container(pool, path=sub_cont))
            self.insert_files_and_verify(
                hosts, os.path.join(mount_dir, f"cont{idx + 1}"), tmp_file_count, tmp_file_name,
                tmp_file_size)
        expected = pool_space_before - cont_count * tmp_file_count * tmp_file_size
        pool_space_after = pool.get_pool_free_space(device)
        self.log.info("Pool space <= Expected")
        self.log.info("%s <= %s", pool_space_after, expected)
        self.assertTrue(pool_space_after <= expected)
        self.log.info("Destroying half of the containers = %s", cont_count // 2)
        for _ in range(cont_count // 2):
            containers[-1].destroy(1)
            containers.pop()
        expected = pool_space_after + ((cont_count // 2) * tmp_file_count * tmp_file_size)
        pool_space_after_cont_destroy = pool.get_pool_free_space(device)
        self.log.info("After container destroy")
        self.log.info("Free Pool space >= Expected")
        self.log.info("%s >= %s", pool_space_after_cont_destroy, expected)
        self.assertTrue(pool_space_after_cont_destroy >= expected)

    def insert_files_and_verify(self, hosts, cont_dir, tmp_file_count, tmp_file_name,
                                tmp_file_size):
        """Verify inserting files into a specific container.

        Insert files into the specific container and verify they're navigable
        and accessible.

        Args:
            hosts (NodeSet): Hosts on which to run the commands
            cont_dir (str): container directory
            tmp_file_count (int): number of temporary files
            tmp_file_name (str): base name for temporary files
            tmp_file_size (int): size of temporary files
        """
        cmds = []
        ls_cmds = []

        for idx in range(tmp_file_count):
            # Create 40 MB files
            file_name = f"{tmp_file_name}{idx + 1}"
            cmd = f"head -c {tmp_file_size} /dev/urandom > {cont_dir}/{file_name}"
            ls_cmds.append(f"ls {file_name}")
            cmds.append(cmd)
        self._execute_cmd(";".join(cmds), hosts)

        cmds = []
        # Run ls to verify the temp files are actually created
        cmds = [f"cd {cont_dir}"]
        cmds.extend(ls_cmds)
        self._execute_cmd(";".join(cmds), hosts)

    def _execute_cmd(self, cmd, hosts):
        """Execute command on the host clients.

        Args:
            cmd (str): Command to run
            hosts (NodeSet): hosts on which to run the command

        """
        result = run_remote(self.log, hosts, cmd, timeout=30)
        if not result.passed:
            self.fail(f"Error running '{cmd}' on {str(result.failed_hosts)}")
