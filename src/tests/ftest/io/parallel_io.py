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

import threading
import subprocess
import time
from getpass import getuser
import general_utils

from ClusterShell.NodeSet import NodeSet
from command_utils import CommandFailure
from fio_test_base import FioBase
from ior_test_base import IorTestBase


# pylint: disable=too-many-ancestors
class ParallelIo(FioBase, IorTestBase):
    """Base Parallel IO test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ParallelIo object."""
        super(ParallelIo, self).__init__(*args, **kwargs)
        self.dfuse = None
        self.cont_count = None
        self.pool_count = None
        self.statvfs_info_initial = None
        self.statvfs_before_cont_destroy = None
        self.statvfs_after_cont_destroy = None
        self.pool = []
        self.container = []

    def create_pool(self):
        """Create a TestPool object to use with ior."""
        self.pool.append(self.get_pool(connect=False))

    # pylint: disable=arguments-differ
    def create_cont(self, pool):
        """Create a TestContainer object to be used to create container.

        Args:
            pool (TestPool): TestPool object type for which container
                             needs to be created
        """
        self.container.append(self.get_container(pool))

    def stat_bfree(self, path):
        """Get stat bfree.

        Args:
            path (str): path to get free block size of.

        Returns:
            int: value of stat free blocks

        """
        cmd = ["ssh", "{}@{}".format(getuser(), self.hostlist_clients[0]),
               "stat -c%a -f {}".format(path)]
        try:
            result = subprocess.check_output(cmd)
        except subprocess.CalledProcessError as err:
            self.fail("Get free block size method failed with: {}".format(err))

        return int(result)

    def statvfs_pool(self, path):
        """Obtain the free space for the pool using statvfs.

        Args:
            path (str): path for which free space needs to be obtained for.

        Returns:
            list: list of free space info for each pool supplied in pool_obj.

        """
        statvfs_list = []
        for _, pool in enumerate(self.pool):
            dfuse_pool_dir = str(path + "/" + pool.uuid)
            statvfs_info = self.stat_bfree(dfuse_pool_dir)
            statvfs_list.append(statvfs_info)
            self.log.info("Statvfs List Output: %s", statvfs_list)

        return statvfs_list

    def verify_aggregation(self, reduced_space, count):
        """Verify aggregation.

        Verify if expected space is returned for each pool after containers
        were destroyed. If not, wait for 60 secs and check again. Wait 4 times,
        otherwise exit the test with a failure.

        Args:
            reduced_space (int): expected space to be returned
            count (int): aggregation index
        """
        counter = 1
        while (self.statvfs_after_cont_destroy[count] <
               self.statvfs_before_cont_destroy[count] + reduced_space):
            # try to wait for 4 x 60 secs for aggregation to be completed
            # or else exit the test with a failure.
            if counter > 4:
                self.log.info("Free space before io: %s",
                              self.statvfs_info_initial)
                self.log.info("Free space after io: %s",
                              self.statvfs_before_cont_destroy)
                self.log.info("Free space at test termination: %s",
                              self.statvfs_after_cont_destroy)
                self.fail("Aggregation did not complete as expected")
            time.sleep(60)
            self.statvfs_after_cont_destroy = self.statvfs_pool(
                self.dfuse.mount_dir.value)
            counter += 1

    def test_parallelio(self):
        """Jira ID: DAOS-3775.

        Test Description:
            Purpose of this test is to mount dfuse and verify multiple
            containers using fio.
        Use cases:
            Mount dfuse using pool uuid.
            Create multiple containers under that dfuse mount point.
            Check those containers are accessible from that mount point.
            Perform io to those containers using FIO
            Delete one of the containers
            Check if dfuse is still running. If not, fail the test and exit.
            Otherwise, try accessing the deleted container.
            This should fail.
            Check dfuse again.
        :avocado: tags=all,hw,daosio,medium,ib2,full_regression,parallelio
        """
        # get test params for cont and pool count
        self.cont_count = self.params.get("cont_count", '/run/container/*')

        threads = []

        # Create a pool and start dfuse.
        self.create_pool()
        self.start_dfuse(self.hostlist_clients, self.pool[0], None)
        # create multiple containers
        for _ in range(self.cont_count):
            self.create_cont(self.pool[0])

        # check if all the created containers can be accessed and perform
        # io on each container using fio in parallel
        for _, cont in enumerate(self.container):
            dfuse_cont_dir = self.dfuse.mount_dir.value + "/" + cont.uuid
            cmd = u"ls -a {}".format(dfuse_cont_dir)
            try:
                # execute bash cmds
                ret_code = general_utils.pcmd(
                    self.hostlist_clients, cmd, timeout=30)
                if 0 not in ret_code:
                    error_hosts = NodeSet(
                        ",".join(
                            [str(node_set) for code, node_set in
                             ret_code.items() if code != 0]))
                    raise CommandFailure(
                        "Error running '{}' on the following "
                        "hosts: {}".format(cmd, error_hosts))
            # report error if any command fails
            except CommandFailure as error:
                self.log.error("ParallelIo Test Failed: %s",
                               str(error))
                self.fail("Test was expected to pass but "
                          "it failed.\n")
            # run fio on all containers
            thread = threading.Thread(target=self.execute_fio, args=(
                self.dfuse.mount_dir.value + "/" + cont.uuid, False))
            threads.append(thread)
            thread.start()

        # wait for all fio jobs to be finished
        for job in threads:
            job.join()

        # destroy first container
        container_to_destroy = self.container[0].uuid
        self.container[0].destroy(1)

        # check dfuse if it is running fine
        self.dfuse.check_running()

        # try accessing destroyed container, it should fail
        try:
            self.execute_fio(
                self.dfuse.mount_dir.value + "/" + container_to_destroy, False)
            self.fail(
                "Fio was able to access destroyed container: {}".format(
                    self.container[0].uuid))
        except CommandFailure as error:
            self.log.info("This run is expected to fail")

        # check dfuse is still running after attempting to access deleted
        # container.
            self.dfuse.check_running()

    def test_multipool_parallelio(self):
        """Jira ID: DAOS-3775.

        Test Description:
            Purpose of this test is to verify aggregation across multiple
            pools and containers.
        Use cases:
            Create 10 pools
            Create 10 containers under each pool.
            Record statvfs free space for each pool.
            Perform parallel io to each pool without deleting the file
            after write.
            Record free space using statvfs after write.
            Delete half of the containers from each pool.
            Calculate the expected amount of data to be deleted when
            containers are destroyed.
            Record free space after container destroy.
            Loop until either the all space is returned back after aggregation
            completion or exit the loop after trying for 240 secs of wait and
            fail the test.

        :avocado: tags=all,hw,daosio,medium,ib2,full_regression
        :avocado: tags=multipoolparallelio
        """
        # test params
        threads = []
        pool_threads = []
        cont_threads = []
        self.pool_count = self.params.get("pool_count", '/run/pool/*')
        self.cont_count = self.params.get("cont_count", '/run/container/*')
        processes = self.params.get("np", '/run/ior/client_processes/*')

        # Create pools in parallel.
        for _ in range(self.pool_count):
            pool_thread = threading.Thread(target=self.create_pool())
            pool_threads.append(pool_thread)
            pool_thread.start()
        # wait for container create to finish
        for pool_job in pool_threads:
            pool_job.join()

        # start dfuse using --svc option only.
        self.start_dfuse(self.hostlist_clients, None, None)

        # record free space using statvfs before any data is written.
        self.statvfs_info_initial = self.statvfs_pool(
            self.dfuse.mount_dir.value)

        # Create 10 containers for each pool. Container create process cannot
        # be parallelised as different container create could complete at
        # different times and get appended in the self.container variable in
        # unorderly manner, causing problems during the write process.
        for _, pool in enumerate(self.pool):
            for _ in range(self.cont_count):
                self.create_cont(pool)

        # Try to access each dfuse mounted container using ls. Once it is
        # accessed successfully, go ahead and perform io on that location
        # using ior. This process of performing io is done in parallel for
        # all containers using threads.
        for pool_count, pool in enumerate(self.pool):
            dfuse_pool_dir = str(self.dfuse.mount_dir.value + "/" + pool.uuid)
            for counter in range(self.cont_count):
                cont_num = (pool_count * self.cont_count) + counter
                dfuse_cont_dir = str(dfuse_pool_dir + "/" +
                                     self.container[cont_num].uuid)
                cmd = u"###ls -a {}".format(dfuse_cont_dir)
                self.execute_cmd(cmd)

                # run ior on all containers
                test_file = dfuse_cont_dir + "/testfile"
                self.ior_cmd.test_file.update(test_file)
                self.ior_cmd.set_daos_params(
                    self.server_group, pool, self.container[cont_num].uuid)
                thread = threading.Thread(
                    target=self.run_ior,
                    args=(self.get_ior_job_manager_command(), processes, None,
                          False))
                threads.append(thread)
                thread.start()

        # wait for all ior jobs to be finished
        for job in threads:
            job.join()

        # Record free space after io
        self.statvfs_before_cont_destroy = self.statvfs_pool(
            self.dfuse.mount_dir.value)

        # Destroy half of the containers from each pool
        pfinal = 0
        for count in range(self.cont_count):
            pinitial = pfinal
            pfinal = pinitial + (self.cont_count // 2)
            del self.container[pinitial:pfinal]

        for cont in self.container:
            cont_thread = threading.Thread(target=cont.destroy)
            cont_threads.append(cont_thread)
            cont_thread.start()

        for destroy_job in cont_threads:
            destroy_job.join()

        # Record free space after container destroy.
        self.statvfs_after_cont_destroy = self.statvfs_pool(
            self.dfuse.mount_dir.value)

        # Calculate the expected space to be returned after containers
        # are destroyed.
        reduced_space = (self.cont_count *
                         int(self.ior_cmd.block_size.value))/2

        # Verify if expected space is returned for each pool after containers
        # were destroyed. If not, wait for 60 secs and check again. Wait 4
        # times, otherwise exit the test with a failure.
        for count in range(self.pool_count):
            thread = threading.Thread(
                target=self.verify_aggregation,
                args=(reduced_space, count))
            threads.append(thread)
            thread.start()

        for job in threads:
            job.join()
