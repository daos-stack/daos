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
import general_utils

from ClusterShell.NodeSet import NodeSet
from command_utils import CommandFailure
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from dfuse_utils import Dfuse
from fio_test_base import FioBase

class ParallelIo(FioBase):
    """Base Parallel IO test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ParallelIo object."""
        super(ParallelIo, self).__init__(*args, **kwargs)
        self.dfuse = None
        self.cont_count = None
        self.container = []

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(ParallelIo, self).setUp()

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.dfuse:
                self.dfuse.stop()
        finally:
            # Stop the servers and agents
            super(ParallelIo, self).tearDown()

    def create_pool(self):
        """Create a TestPool object to use with ior."""
        # Get the pool params
        self.pool = TestPool(
            self.context, dmg_command=self.get_dmg_command())
        self.pool.get_params(self)

        # Create a pool
        self.pool.create()

    def create_cont(self):
        """Create a TestContainer object to be used to create container."""
        # Get container params
        container = TestContainer(
            self.pool, daos_command=DaosCommand(self.bin))
        container.get_params(self)

        # create container
        container.create()
        self.container.append(container)

    def start_dfuse(self):
        """Create a DfuseCommand object to start dfuse.
        """

        # Get Dfuse params
        self.dfuse = Dfuse(self.hostlist_clients, self.tmp)
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(self.pool)
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

    def test_parallelio(self):
        """Jira ID: DAOS-3775.

        Test Description:
            Purpose of this test is to mount dfuse and verify multiple
            containers using fio.
        Use cases:
            Mount dfuse using pool uuid.
            create multiple containers under that dfuse mount point.
            Check those container are accessable from that mount point.
            Perform io to those containers using FIO
            Delete one of the containers
            Check if dfuse is still running.
            If not, fail otherwise, try accessing the deleted container.
            This should fail.
            Check dfuse again.
        :avocado: tags=all,hw,daosio,medium,ib2,full_regression,parallelio
        """
        # get test params for cont and pool count
        self.cont_count = self.params.get("cont_count", '/run/container/*')

        threads = []

        # Create a pool and start dfuse.
        self.create_pool()
        self.start_dfuse()
        # create multiple containers in parallel
        cont_threads = []
        for _ in range(self.cont_count):
            cont_thread = threading.Thread(target=self.create_cont())
            cont_threads.append(cont_thread)
        # start container create job
        for cont_job in cont_threads:
            cont_job.start()
        # wait for container create to finish
        for cont_job in cont_threads:
            cont_job.join()

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
            self.execute_fio(self.dfuse.mount_dir.value + "/" + \
                container_to_destroy, False)
            self.fail("Fio was able to access destroyed container: {}".\
                format(self.container[0].uuid))
        except CommandFailure as error:
            self.log.info("This run is expected to fail")

        # check dfuse is still running after attempting to access deleted
        # container.
            self.dfuse.check_running()
