#!/usr/bin/python
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
import os
import subprocess
import general_utils

from ClusterShell.NodeSet import NodeSet
from apricot import TestWithServers
from ior_utils import IorCommand
from command_utils import Mpirun, CommandFailure
from mpio_utils import MpioUtils
from test_utils import TestPool
from dfuse_utils import Dfuse
import write_host_file

class BashCmd(TestWithServers):
    """Base BashCmd test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a BashCmd object."""
        super(BashCmd, self).__init__(*args, **kwargs)
        self.dfuse = None
        self.container = None
        self.file_name = None
        self.dir_name = None

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
            self.dfuse = None
        finally:
            # Stop the servers and agents
            super(BashCmd, self).tearDown()

    def create_pool(self):
        """Create a TestPool object to use with ior."""
        # Get the pool params
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)

        # Create a pool
        self.pool.create()

    def create_cont(self):
        """Create a TestContainer object to be used to create container."""
        # TO-DO: Enable container using TestContainer object,
        # once DAOS-3355 is resolved.
        # Get Container params
        #self.container = TestContainer(self.pool)
        #self.container.get_params(self)

        # create container
        # self.container.create()
        env = Dfuse(self.hostlist_clients, self.tmp).get_default_env()
        # command to create container of posix type
        cmd = env + "daos cont create --pool={} --svc={} --type=POSIX".format(
            self.pool.uuid, ":".join(
                [str(item) for item in self.pool.svc_ranks]))
        try:
            container = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                         shell=True)
            (output, err) = container.communicate()
            self.log.info("Container created with UUID %s", output.split()[3])

        except subprocess.CalledProcessError as err:
            self.fail("Container create failed:{}".format(err))

        return output.split()[3]

    def start_dfuse(self):
        """Create a DfuseCommand object to start dfuse."""

        # Get Dfuse params
        self.dfuse = Dfuse(self.hostlist_clients[:-1], self.tmp, True)
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(self.pool)
        self.dfuse.set_dfuse_cont_param(self.create_cont())

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
        """Execute ior with optional overrides for ior flags and object_class.

        If specified the ior flags and ior daos object class parameters will
        override the values read from the yaml file.

        Args:
            ior_flags (str, optional): ior flags. Defaults to None.
            object_class (str, optional): daos object class. Defaults to None.

        :avocado: tags=all,daosio,small,pr,hw,bashcmd
        """
        # Create a pool if one does not already exist
        if self.pool is None:
            self.create_pool()

        self.start_dfuse()
        abs_dir_path = self.dfuse.mount_dir.value + "/" + self.dir_name
        abs_file_path1 = abs_dir_path + "/" + self.file_name1
        abs_file_path2 = abs_dir_path + "/" + self.file_name2
        dir_exists, _ = general_utils.check_file_exists(
            self.hostlist_clients[:-1], abs_dir_path, directory=True)
        if not dir_exists:
            commands = ["mkdir -p {}".format(abs_dir_path),
                        "touch {}".format(abs_file_path1),
                        "ls -a {}".format(abs_file_path1),
                        "chmod -R 0777 {}".format(abs_dir_path),
                        "rm {}".format(abs_file_path1),
                        "dd if=/dev/zero of={} count={} bs={}".format(abs_file_path1, self.dd_count, self.dd_blocksize),
                        "ls -al {}".format(abs_file_path1),
                        "filesize=$(stat -c%s '{}'); if (( filesize != {}*{} )); then exit 1; fi".format(abs_file_path1, self.dd_count, self.dd_blocksize),
                        "cat {}".format(abs_file_path1),
                        "cp -r {} {}".format(abs_file_path1, abs_file_path2),
                        "cmp --silent {} {}".format(abs_file_path1, abs_file_path2),
                        "rm {}".format(abs_file_path2),
                        "mv {} {}".format(abs_file_path1, abs_file_path2),
                        "ls -al {}".format(abs_file_path2),
                        "rm {}".format(abs_file_path2),
                        "rmdir {}".format(abs_dir_path)]
            for cmd in commands:
                try:
                    ret_code = general_utils.pcmd(self.hostlist_clients[:-1], cmd, timeout=30)
                    if 0 not in ret_code:
                        error_hosts = NodeSet(
                            ",".join(
                                [str(node_set) for code, node_set in ret_code.items()
                                 if code != 0]))
                        raise CommandFailure(
                            "Error running '{}' on the following "
                            "hosts: {}".format(cmd, error_hosts))
                    
                except CommandFailure as error:
                    self.log.error("BashCmd Test Failed: %s", str(error))
                    self.fail("Test was expected to pass but it failed.\n")
