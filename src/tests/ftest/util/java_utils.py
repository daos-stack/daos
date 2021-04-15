#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from general_utils import run_pcmd

class JavaUtils(TestWithServers):
    """
        Base class for Java test code
        :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a JavaUtils object."""
        super().__init__(*args, **kwargs)
        self.pool = None
        self.container = None
        self.jdir = None

    def _create_pool(self):
        """Create a TestPool object.
        """
        # Get the pool params
        pool = TestPool(
            self.context, dmg_command=self.get_dmg_command())
        pool.get_params(self)
        # Create a pool
        pool.create()
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
        return container

    def java_version(self):
        """Check if java is installed.

        Returns:
            bool: whether java is installed or not.

        """
        # look for java home
        command = [u"source {}/daos-java/find_java_home.sh".format(self.jdir)]
        return self.run_command(self.hostlist_clients, command)

    def run_command(self, hosts, commands, verbose=True, timeout=60):
        """Method to execute run_pcmd and return a
           failure if the cmd returns non zero value.
           Args:
               hosts: list of hosts cmd needs to be run on
               commands (list): Commands to run
               verbose (bool): Display output. Default is True
               timeout (int): Timeout for a particular command.
                              Default is 60 secs
           Returns:
               Returns command output
        """

        for cmd in commands:
            task = run_pcmd(hosts, cmd, verbose, timeout)
            for result in task:
                if result["exit_status"] != 0:
                    self.log.info(result["stdout"])
                    self.fail("Failed to run cmd {} on {}".\
                              format(cmd, result["hosts"]))
        if result["stdout"]:
            return str(result["stdout"][0])
