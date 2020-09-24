#!/usr/bin/python
"""
  (C) Copyright 2019-2020 Intel Corporation.

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
import time

from command_utils_base import CommandFailure, FormattedParameter
from command_utils import ExecutableCommand
from ClusterShell.NodeSet import NodeSet
from general_utils import check_file_exists, pcmd


class DfuseCommand(ExecutableCommand):
    """Defines a object representing a dfuse command."""

    def __init__(self, namespace, command):
        """Create a dfuse Command object."""
        super(DfuseCommand, self).__init__(namespace, command)

        # dfuse options
        self.puuid = FormattedParameter("--pool {}")
        self.cuuid = FormattedParameter("--container {}")
        self.mount_dir = FormattedParameter("--mountpoint {}")
        self.svcl = FormattedParameter("--svc {}", 0)
        self.sys_name = FormattedParameter("--sys-name {}")
        self.singlethreaded = FormattedParameter("--singlethreaded", False)
        self.foreground = FormattedParameter("--foreground", False)
        self.disable_direct_io = FormattedParameter("--disable-direct-io",
                                                    False)

        # Environment variable names to export when running dfuse
        self._env_names = ["D_LOG_FILE"]

    def set_dfuse_params(self, pool, display=True):
        """Set the dfuse params for the DAOS group, pool, and container uuid.

        Args:
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.set_dfuse_pool_params(pool, display)

    def set_dfuse_pool_params(self, pool, display=True):
        """Set Dfuse params based on Daos Pool.

        Args:
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.puuid.update(pool.uuid, "puuid" if display else None)
        self.set_dfuse_svcl_param(pool, display)

    def set_dfuse_svcl_param(self, pool, display=True):
        """Set the dfuse svcl param from the ranks of a DAOS pool object.

        Args:
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """
        svcl = ":".join(
            [str(item) for item in [
                int(pool.pool.svc.rl_ranks[index])
                for index in range(pool.pool.svc.rl_nr)]])
        self.svcl.update(svcl, "svcl" if display else None)

    def set_dfuse_cont_param(self, cont, display=True):
        """Set dfuse cont param from Container object.

        Args:
            cont (TestContainer): Daos test container object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.cuuid.update(cont, "cuuid" if display else None)

    def set_dfuse_exports(self, manager, log_file):
        """Set exports to issue before the dfuse command.

        Args:
            manager (DaosServerManager): server manager object to use to
                obtain the ofi and cart environmental variable settings from the
                server yaml file
            log_file (str): name of the log file to combine with the
                DAOS_TEST_LOG_DIR path with which to assign D_LOG_FILE
        """
        env = self.get_environment(manager, log_file)
        self.set_environment(env)


class Dfuse(DfuseCommand):
    """Class defining an object of type DfuseCommand."""

    def __init__(self, hosts, tmp):
        """Create a dfuse object."""
        super(Dfuse, self).__init__("/run/dfuse/*", "dfuse")

        # set params
        self.hosts = hosts
        self.tmp = tmp
        self.running_hosts = NodeSet()

    def __del__(self):
        """Destruct the object."""
        if len(self.running_hosts):
            self.log.error('Dfuse object deleted without shutting down')

    def create_mount_point(self):
        """Create dfuse directory.

        Raises:
            CommandFailure: In case of error creating directory

        """
        # raise exception if mount point not specified
        if self.mount_dir.value is None:
            raise CommandFailure("Mount point not specified, "
                                 "check test yaml file")

        _, missing_nodes = check_file_exists(
            self.hosts, self.mount_dir.value, directory=True)
        if len(missing_nodes):

            cmd = "mkdir -p {}".format(self.mount_dir.value)
            ret_code = pcmd(missing_nodes, cmd, timeout=30)
            if len(ret_code) > 1 or 0 not in ret_code:
                error_hosts = NodeSet(
                    ",".join(
                        [str(node_set) for code, node_set in ret_code.items()
                         if code != 0]))
                raise CommandFailure(
                    "Error creating the {} dfuse mount point on the following "
                    "hosts: {}".format(self.mount_dir.value, error_hosts))

    def remove_mount_point(self, fail=True):
        """Remove dfuse directory.

        Try once with a simple rmdir which should succeed, if this does not then
        try again with rm -rf, but still raise an error.

        Raises:
            CommandFailure: In case of error deleting directory

        """
        # raise exception if mount point not specified
        if self.mount_dir.value is None:
            raise CommandFailure("Mount point not specified, "
                                 "check test yaml file")

        dir_exists, clean_nodes = check_file_exists(
            self.hosts, self.mount_dir.value, directory=True)
        if dir_exists:

            target_nodes = list(self.hosts)
            if clean_nodes:
                target_nodes.remove(clean_nodes)

            cmd = "rmdir {}".format(self.mount_dir.value)
            ret_code = pcmd(target_nodes, cmd, timeout=30)
            if len(ret_code) == 1 and 0 in ret_code:
                return

            failed_nodes = NodeSet(",".join(
                [str(node_set) for code, node_set in ret_code.items()
                 if code != 0]))

            cmd = "rm -rf {}".format(self.mount_dir.value)
            ret_code = pcmd(failed_nodes, cmd, timeout=30)
            if len(ret_code) > 1 or 0 not in ret_code:
                error_hosts = NodeSet(
                    ",".join(
                        [str(node_set) for code, node_set in ret_code.items()
                         if code != 0]))
                if fail:
                    raise CommandFailure(
                        "Error removing the {} dfuse mount point with rm on "
                        "the following hosts: {}".format(self.mount_dir.value,
                                                         error_hosts))
            if fail:
                raise CommandFailure(
                    "Error removing the {} dfuse mount point with rmdir on the "
                    "following hosts: {}".format(self.mount_dir.value,
                                                 failed_nodes))

    def run(self, check=True):
        """Run the dfuse command.

        Args:
            check (bool): Check if dfuse mounted properly after
                mount is executed.
        Raises:
            CommandFailure: In case dfuse run command fails

        """
        self.log.info('Starting dfuse at %s', self.mount_dir.value)

        # A log file must be defined to ensure logs are captured
        if "D_LOG_FILE" not in self.env:
            raise CommandFailure(
                "Dfuse missing environment variables for D_LOG_FILE")

        # create dfuse dir if does not exist
        self.create_mount_point()

        # run dfuse command
        cmd = "".join([self.env.get_export_str(), self.__str__()])
        ret_code = pcmd(self.hosts, cmd, timeout=30)

        if 0 in ret_code:
            self.running_hosts.add(ret_code[0])
            del ret_code[0]

        if len(ret_code):
            error_hosts = NodeSet(
                ",".join(
                    [str(node_set) for code, node_set in ret_code.items()
                     if code != 0]))
            raise CommandFailure(
                "Error starting dfuse on the following hosts: {}".format(
                    error_hosts))

        if check:
            # Dfuse will block in the command for the mount to complete, even
            # if run in background mode so it should be possible to start using
            # it immediately after the command returns.
            if not self.check_running(fail_on_error=False):
                self.log.info('Waiting two seconds for dfuse to start')
                time.sleep(2)
                if not self.check_running(fail_on_error=False):
                    self.log.info('Waiting five seconds for dfuse to start')
                    time.sleep(5)
                    self.check_running()

    def check_running(self, fail_on_error=True):
        """Check dfuse is running.

        Run a command to verify dfuse is running on hosts where it is supposed
        to be.  Use grep -v and rc=1 here so that if it isn't, then we can
        see what is being used instead.
        """
        retcodes = pcmd(
            self.running_hosts,
            "stat -c %T -f {0} | grep -v fuseblk".format(self.mount_dir.value),
            expect_rc=1)
        if 1 in retcodes:
            del retcodes[1]
        if len(retcodes):
            self.log.error('Errors checking running: %s', retcodes)
            if not fail_on_error:
                return False
            raise CommandFailure('dfuse not running')
        return True

    def stop(self):
        """Stop dfuse.

        Try to stop dfuse.  Try once nicely by using fusermount, then if that
        fails try to pkill it to see if that works.  Abort based on the result
        of the fusermount, as if pkill is necessary then dfuse itself has
        not worked correctly.

        Finally, try and remove the mount point, and that itself should work.

        Raises:
            CommandFailure: In case dfuse stop fails

        """
        self.log.info('Stopping dfuse at %s on %s',
                      self.mount_dir.value,
                      self.running_hosts)

        if self.mount_dir.value is None:
            return

        if not len(self.running_hosts):
            return

        self.check_running()
        umount_cmd = [
            "if [ -x '$(command -v fusermount)' ]",
            "then fusermount -u {0}".format(self.mount_dir.value),
            "else fusermount3 -u {0}".format(self.mount_dir.value),
            "fi"
        ]
        ret_code = pcmd(self.running_hosts, "; ".join(umount_cmd), timeout=30)

        if 0 in ret_code:
            self.running_hosts.remove(ret_code[0])
            del ret_code[0]

        if len(self.running_hosts):
            cmd = "pkill dfuse --signal KILL"
            pcmd(self.running_hosts, cmd, timeout=30)
            pcmd(self.running_hosts, umount_cmd, timeout=30)
            self.remove_mount_point(fail=False)
            raise CommandFailure(
                "Error stopping dfuse on the following hosts: {}".format(
                    self.running_hosts))
        time.sleep(2)
        self.remove_mount_point()
