#!/usr/bin/python
'''
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
'''
from __future__ import print_function

import os

from apricot import TestWithServers
from daos_api import DaosPool
from ior_utils import IorCommand, IorFailed
from mpio_utils import MpioUtils
import write_host_file


class IorTestBase(TestWithServers):
    """Base IOR test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a IorTestBase object."""
        super(IorTestBase, self).__init__(*args, **kwargs)
        self.ior_cmd = None
        self.processes = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(IorTestBase, self).setUp()

        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file.write_host_file(
            self.hostlist_clients, self.workdir, None)

        # Get the parameters for IOR
        self.ior_cmd = IorCommand()
        self.ior_cmd.set_params(self)
        self.processes = self.params.get("np", '/run/ior/client_processes/*')

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.pool is not None and self.pool.attached:
                self.pool.destroy(1)
        finally:
            # Stop the servers and agents
            super(IorTestBase, self).tearDown()

    def execute_ior(self, ior_flags=None, object_class=None):
        """Execute ior with optional overrides for ior flags and object_class.

        If specified the ior flags and ior daos object class parameters will
        override the values read from the yaml file.

        Args:
            ior_flags (str, optional): ior flags. Defaults to None.
            object_class (str, optional): daos object class. Defaults to None.
        """
        # Get the parameters used to create a pool
        mode = self.params.get("mode", "/run/pool/*")
        uid = os.geteuid()
        gid = os.getegid()
        group = self.params.get("setname", "/run/pool/*", self.server_group)
        scm_size = self.params.get("scm_size", "/run/pool/*")
        nvme_size = self.params.get("nvme_size", "/run/pool/*", 0)
        svcn = self.params.get("svcn", "/run/pool/*", 1)

        # Initialize a python pool object then create the underlying
        # daos storage
        self.pool = DaosPool(self.context)
        self.pool.create(
            mode, uid, gid, scm_size, group, None, None, svcn, nvme_size)

        # Initialize MpioUtils if IOR is running in MPIIO mode
        if self.ior_cmd.api.value == "MPIIO":
            mpio_util = MpioUtils()
            if mpio_util.mpich_installed(self.hostlist_clients) is False:
                self.fail("Exiting Test: Mpich not installed")
            path = mpio_util.mpichinstall
        else:
            path = None

        # Override the yaml IOR params with provided values
        if ior_flags:
            self.ior_cmd.flags.value = ior_flags
        if object_class:
            self.ior_cmd.daos_oclass.value = object_class

        # Run IOR
        self.ior_cmd.set_daos_params(self.server_group, self.pool)
        try:
            self.ior_cmd.run(
                self.basepath, self.processes, self.hostfile_clients, True,
                path)
        except IorFailed as excep:
            print(excep)
            self.fail("Test was expected to pass but it failed.\n")


class IorSingleServer(IorTestBase):
    """Test class Description: Runs IOR with 1 server.

    :avocado: recursive
    """

    def test_singleserver(self):
        """Jira ID: DAOS-XXXX.

        Test Description:
            Test IOR with Single Server config.

        Use Cases:
            Different combinations of 1/64/128 Clients,
            1K/4K/32K/128K/512K/1M transfer size.

        :avocado: tags=ior,singleserver
        """
        self.execute_ior()
