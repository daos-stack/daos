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
import os
import general_utils

from ClusterShell.NodeSet import NodeSet
from apricot import TestWithServers
from command_utils import CommandFailure
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from dfuse_utils import Dfuse


class BashCmd(TestWithServers):
    """Base BashCmd test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a BashCmd object."""
        super(BashCmd, self).__init__(*args, **kwargs)
        self.dfuse = None
        self.file_name = None
        self.dir_name = None
        self.pool_count = None
        self.cont_count = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(BashCmd, self).setUp()

        # Get the parameters for BashCmd
        self.dir_name = self.params.get("dirname", '/run/bashcmd/*')
        self.file_name1 = self.params.get("filename1", '/run/bashcmd/*')
        self.file_name2 = self.params.get("filename2", '/run/bashcmd/*')
        self.dd_count = self.params.get("dd_count", '/run/bashcmd/*')
        self.dd_blocksize = self.params.get("dd_blocksize", '/run/bashcmd/*')

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.dfuse:
                self.dfuse.stop()
        finally:
            # Stop the servers and agents
            super(BashCmd, self).tearDown()

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
        self.container = TestContainer(
            self.pool, daos_command=DaosCommand(self.bin))
        self.container.get_params(self)

        # create container
        self.container.create()

    def start_dfuse(self, count):
        """Create a DfuseCommand object to start dfuse.

           Args:
             count(int): container index
        """

        # Get Dfuse params
        self.dfuse = Dfuse(self.hostlist_clients[:-1], self.tmp)
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.mount_dir.update("/tmp/" + self.pool.uuid + "_daos_dfuse"
                                    + str(count))
        self.dfuse.set_dfuse_params(self.pool)
        self.dfuse.set_dfuse_cont_param(self.container)
        self.dfuse.set_dfuse_exports(self.server_managers[0], self.client_log)

        try:
            # start dfuse
            self.dfuse.run()
        except CommandFailure as error:
            self.log.error("Dfuse command %s failed on hosts %s",
                           str(self.dfuse),
                           str(NodeSet.fromlist(self.dfuse.hosts)),
                           exc_info=error)
            self.fail("Test was expected to pass but it failed.\n")

    def test_bashcmd(self):
        """Jira ID: DAOS-3508.

        Test Description:
            Purpose of this test is to mount different mount points of dfuse
            for different container and pool sizes and perform basic bash
            commands.
        Use cases:
            Folloing list of bash commands have been incorporated
            as part of this test: mkdir, touch, ls, chmod, rm, dd, stat,
            cp, cmp, mv, rmdir.
              Create a directory.
              Create a file under that directory.
              List the created file.
              Remove the file.
              Write a file to the dfuse mounted location using dd.
              List the written file to verify if it's create.
              Verify the file created is of right size as desired.
              Copy the file
              Compare the copied file with original to verify the
              content is same.
              Remove copied file.
              Rename file
              Verify renamed file exist using list.
              Remove a directory
        :avocado: tags=all,hw,daosio,medium,ib2,full_regression,bashcmd
        """
        self.cont_count = self.params.get("cont_count", '/run/container/*')
        self.pool_count = self.params.get("pool_count", '/run/pool/*')

        # Create a pool if one does not already exist.
        for _ in range(self.pool_count):
            self.create_pool()
            self.create_cont()
            # perform test for multiple containers.
            for count in range(self.cont_count):
                self.start_dfuse(count)
                abs_dir_path = os.path.join(
                    self.dfuse.mount_dir.value, self.dir_name)
                abs_file_path1 = os.path.join(abs_dir_path, self.file_name1)
                abs_file_path2 = os.path.join(abs_dir_path, self.file_name2)
                # check if the dir exists.
                dir_exists, _ = general_utils.check_file_exists(
                    self.hostlist_clients[:-1], abs_dir_path, directory=True)
                # if doesn't exist perform some bash cmds.
                if not dir_exists:
                    # list of commands to be executed.
                    commands = [u"mkdir -p {}".format(abs_dir_path),
                                u"touch {}".format(abs_file_path1),
                                u"ls -a {}".format(abs_file_path1),
                                u"rm {}".format(abs_file_path1),
                                u"dd if=/dev/zero of={} count={} bs={}".format(
                                    abs_file_path1, self.dd_count,
                                    self.dd_blocksize),
                                u"ls -al {}".format(abs_file_path1),
                                u"filesize=$(stat -c%s '{}');\
                                if (( filesize != {}*{} )); then exit 1;\
                                fi".format(abs_file_path1, self.dd_count,
                                           self.dd_blocksize),
                                u"cp -r {} {}".format(abs_file_path1,
                                                      abs_file_path2),
                                u"cmp --silent {} {}".format(abs_file_path1,
                                                             abs_file_path2),
                                u"rm {}".format(abs_file_path2),
                                u"mv {} {}".format(abs_file_path1,
                                                   abs_file_path2),
                                u"ls -al {}".format(abs_file_path2),
                                u"rm {}".format(abs_file_path2),
                                u"rmdir {}".format(abs_dir_path)]
                    for cmd in commands:
                        try:
                            # execute bash cmds
                            ret_code = general_utils.pcmd(
                                self.hostlist_clients[:-1], cmd, timeout=30)
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
                            self.log.error("BashCmd Test Failed: %s",
                                           str(error))
                            self.fail("Test was expected to pass but "
                                      "it failed.\n")
                # stop dfuse
                self.dfuse.stop()
            # destroy container and pool
            self.container.destroy()
            self.pool.destroy()
