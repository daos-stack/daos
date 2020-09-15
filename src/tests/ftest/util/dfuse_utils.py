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

    def check_mount_state(self, nodes=None):
        """Check the dfuse mount point mounted state on the hosts.

        Args:
            nodes (NodeSet, optional): hosts on which to check if dfuse is
                mounted. Defaults to None, which will use all of the hosts.

        Returns:
            dict: a dictionary of NodeSets of hosts with the dfuse mount point
                either "mounted" or "unmounted"

        """
        state = {
            "mounted": NodeSet(),
            "unmounted": NodeSet(),
            "nodirectory": NodeSet()
        }
        if not nodes:
            nodes = NodeSet.fromlist(self.hosts)
        check_mounted = NodeSet()

        # Detect which hosts have mount point directories defined
        command = "test -d {0} -a ! -L {0}".format(self.mount_dir.value)
        retcodes = pcmd(nodes, command, expect_rc=None)
        for retcode, hosts in retcodes.items():
            for host in hosts:
                if retcode == 0:
                    check_mounted.add(host)
                else:
                    state["nodirectory"].add(host)

        if check_mounted:
            # Detect which hosts with mount point directories have it mounted as
            # a fuseblk device
            command = "stat -c %T -f {0} | grep -v fuseblk".format(
                self.mount_dir.value)
            retcodes = pcmd(check_mounted, command, expect_rc=None)
            for retcode, hosts in retcodes.items():
                for host in hosts:
                    if retcode == 1:
                        state["mounted"].add(host)
                    else:
                        state["unmounted"].add(host)

        return state

    def get_umount_command(self, force=False):
        """Get the command to umount the dfuse mount point.

        Args:
            force (bool, optional): whether to force the umount with a lazy
                unmount. Defaults to False.

        Returns:
            str: the dfuse umount command

        """
        umount = "-uz" if force else "-u"
        command = [
            "if [ -x '$(command -v fusermount)' ]",
            "then fusermount {0} {1}".format(umount, self.mount_dir.value),
            "else fusermount3 {0} {1}".format(umount, self.mount_dir.value),
            "fi"
        ]
        return ";".join(command)

    def create_mount_point(self):
        """Create dfuse directory.

        Raises:
            CommandFailure: In case of error creating directory

        """
        # Raise exception if mount point not specified
        if self.mount_dir.value is None:
            raise CommandFailure("Mount point not specified, "
                                 "check test yaml file")

        # Create the mount point on any host without dfuse already mounted
        state = self.check_mount_state()
        if state["nodirectory"]:
            command = "mkdir -p {}".format(self.mount_dir.value)
            ret_code = pcmd(state["nodirectory"], command, timeout=30)
            if len(ret_code) > 1 or 0 not in ret_code:
                failed_nodes = [
                    str(node_set) for code, node_set in ret_code.items()
                    if code != 0
                ]
                error_hosts = NodeSet(",".join(failed_nodes))
                raise CommandFailure(
                    "Error creating the {} dfuse mount point on the "
                    "following hosts: {}".format(
                        self.mount_dir.value, error_hosts))

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

            self.log.info(
                "Removing the %s dfuse mount point on %s",
                self.mount_dir.value, target_nodes)

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
        else:
            self.log.info(
                "No %s dfuse mount point directory found on %s",
                self.mount_dir.value, self.hosts)

    def run(self, check=True):
        # pylint: disable=arguments-differ
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

        Args:
            fail_on_error (bool, optional): should an exception be raised if an
                error is detected. Defaults to True.

        Raises:
            CommandFailure: raised if dfuse is found not running on any expected
                nodes and fail_on_error is set.

        Returns:
            bool: whether or not dfuse is running

        """
        status = True
        state = self.check_mount_state(self.running_hosts)
        if state["unmounted"] or state["nodirectory"]:
            self.log.error(
                "Error: dfuse not running on %s",
                str(state["unmounted"].union(state["nodirectory"])))
            status = False
            if fail_on_error:
                raise CommandFailure("dfuse not running")
        return status

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
        # Include all hosts when stopping to ensure all mount points in any
        # state are properly removed
        self.running_hosts.add(NodeSet.fromlist(self.hosts))

        self.log.info(
            "Stopping dfuse at %s on %s",
            self.mount_dir.value, self.running_hosts)

        if self.mount_dir.value and self.running_hosts:
            error_list = []

            # Loop until all fuseblk mounted devices are unmounted
            counter = 0
            while self.running_hosts and counter < 3:
                # Attempt to kill dfuse on after first unmount fails
                if self.running_hosts and counter > 1:
                    kill_command = "pkill dfuse --signal KILL"
                    pcmd(self.running_hosts, kill_command, timeout=30)

                # Attempt to unmount any fuseblk mounted devices after detection
                if self.running_hosts and counter > 0:
                    pcmd(
                        self.running_hosts,
                        self.get_umount_command(counter > 1), expect_rc=None)
                    time.sleep(2)

                # Detect which hosts have fuseblk mounted devices and remove any
                # hosts which no longer have the dfuse mount point mounted
                state = self.check_mount_state(self.running_hosts)
                for host in state["unmounted"].union(state["nodirectory"]):
                    self.running_hosts.remove(host)

                # Increment the loop counter
                counter += 1

            if self.running_hosts:
                error_list.append(
                    "Error stopping dfuse on {}".format(self.running_hosts))

            # Remove mount points
            try:
                self.remove_mount_point()
            except CommandFailure as error:
                error_list.append(error)

            # Report any errors
            if error_list:
                raise CommandFailure("\n".join(error_list))

        elif self.mount_dir.value is None:
            self.log.info("No dfuse mount directory defined - nothing to stop")

        else:
            self.log.info("No hosts running dfuse - nothing to stop")
