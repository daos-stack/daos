"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
from ClusterShell.NodeSet import NodeSet

from command_utils_base import FormattedParameter
from exception_utils import CommandFailure
from command_utils import ExecutableCommand
from general_utils import check_file_exists
from run_utils import run_remote


class DfuseCommand(ExecutableCommand):
    """Defines a object representing a dfuse command."""

    def __init__(self, namespace, command):
        """Create a dfuse Command object."""
        super().__init__(namespace, command)

        # dfuse options
        self.puuid = FormattedParameter("--pool {}")
        self.cuuid = FormattedParameter("--container {}")
        self.mount_dir = FormattedParameter("--mountpoint {}")
        self.sys_name = FormattedParameter("--sys-name {}")
        self.thread_count = FormattedParameter("--thread-count {}")
        self.singlethreaded = FormattedParameter("--singlethread", False)
        self.foreground = FormattedParameter("--foreground", False)
        self.enable_caching = FormattedParameter("--enable-caching", False)
        self.enable_wb_cache = FormattedParameter("--enable-wb-cache", False)
        self.disable_caching = FormattedParameter("--disable-caching", False)
        self.disable_wb_cache = FormattedParameter("--disable-wb-cache", False)

        # Environment variable names to export when running dfuse
        self.update_env_names(["D_LOG_FILE"])

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

    def __init__(self, hosts, tmp, namespace="/run/dfuse/*"):
        """Create a dfuse object.

        Args:
            hosts (NodeSet): hosts on which to run dfuse
            tmp (str): tmp directory path
            namespace (str): dfuse namespace. Defaults to /run/dfuse/*
        """
        super().__init__(namespace, "dfuse")

        # set params
        self.hosts = hosts.copy()
        self.tmp = tmp
        self.running_hosts = NodeSet()

    def __del__(self):
        """Destruct the object."""
        if self.running_hosts:
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
            nodes = self.hosts.copy()

        # Detect which hosts have mount point directories defined
        command = f"test -d {self.mount_dir.value} -a ! -L {self.mount_dir.value}"
        test_result = run_remote(self.log, nodes, command)
        check_mounted = test_result.passed_hosts
        if not test_result.passed:
            command = f"grep 'dfuse {self.mount_dir.value}' /proc/mounts"
            grep_result = run_remote(self.log, test_result.failed_hosts, command)
            check_mounted.add(grep_result.passed_hosts)
            state["nodirectory"].add(grep_result.failed_hosts)

        if check_mounted:
            # Detect which hosts with mount point directories have it mounted as a fuseblk device
            command = f"stat -c %T -f {self.mount_dir.value} | grep -v fuseblk"
            stat_result = run_remote(self.log, check_mounted, command)
            for data in stat_result.output:
                if data.returncode == 1:
                    state["mounted"].add(data.hosts)
                else:
                    state["unmounted"].add(data.hosts)

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
            raise CommandFailure("Mount point not specified. Check test yaml file")

        # Create the mount point on any host without dfuse already mounted
        state = self.check_mount_state()
        if state["nodirectory"]:
            command = f"mkdir -p {self.mount_dir.value}"
            result = run_remote(self.log, state["nodirectory"], command, timeout=30)
            if not result.passed:
                raise CommandFailure(
                    f"Error creating the {self.mount_dir.value} dfuse mount point "
                    f"on the following hosts: {result.failed_hosts}")

    def remove_mount_point(self, fail=True):
        """Remove dfuse directory.

        Try once with a simple rmdir which should succeed, if this does not then
        try again with rm -rf, but still raise an error.

        Raises:
            CommandFailure: In case of error deleting directory

        """
        # raise exception if mount point not specified
        if self.mount_dir.value is None:
            raise CommandFailure("Mount point not specified. Check test yaml file")

        dir_exists, clean_nodes = check_file_exists(
            self.hosts, self.mount_dir.value, directory=True)

        if not dir_exists:
            self.log.info(
                "No %s dfuse mount point directory found on %s", self.mount_dir.value, self.hosts)
            return

        target_nodes = self.hosts - clean_nodes

        self.log.info(
            "Removing the %s dfuse mount point on %s", self.mount_dir.value, target_nodes)

        command = f"rmdir {self.mount_dir.value}"
        rmdir_result = run_remote(self.log, target_nodes, command, timeout=30)
        if rmdir_result.passed:
            return

        command = f"rm -rf {self.mount_dir.value}"
        rm_result = run_remote(self.log, rmdir_result.failed_hosts, command, timeout=30)
        if fail and not rm_result.passed:
            raise CommandFailure(
                f"Error removing the {self.mount_dir.value} dfuse mount point with rm on "
                f"the following hosts: {rm_result.failed_hosts}")
        if fail:
            raise CommandFailure(
                f"Error removing the {self.mount_dir.value} dfuse mount point with rmdir on the "
                f"following hosts: {rmdir_result.failed_hosts}")

    def run(self, check=True, bind_cores=None):
        # pylint: disable=arguments-differ
        """Run the dfuse command.

        Args:
            check (bool): Check if dfuse mounted properly after mount is executed.
            bind_cores (str): List of CPU cores to pass to taskset

        Raises:
            CommandFailure: In case dfuse run command fails

        """
        self.log.info('Starting dfuse at %s on %s', self.mount_dir.value, str(self.hosts))

        # A log file must be defined to ensure logs are captured
        if "D_LOG_FILE" not in self.env:
            raise CommandFailure("Dfuse missing environment variables for D_LOG_FILE")

        if 'D_LOG_MASK' not in self.env:
            self.env['D_LOG_MASK'] = 'INFO'

        if 'COVFILE' not in self.env:
            self.env['COVFILE'] = '/tmp/test.cov'

        # create dfuse dir if does not exist
        self.create_mount_point()

        # run dfuse command
        cmd = self.env.to_export_str()
        if bind_cores:
            cmd += 'taskset -c {} '.format(bind_cores)
        cmd += str(self)

        result = run_remote(self.log, self.hosts, cmd, timeout=30)
        self.running_hosts.add(result.passed_hosts)
        if not result.passed:
            raise CommandFailure(
                f"Error starting dfuse on the following hosts: {result.failed_hosts}")

        if check:
            # Dfuse will block in the command for the mount to complete, even
            # if run in background mode so it should be possible to start using
            # it immediately after the command returns.
            num_retries = 3
            for retry in range(1, num_retries + 1):
                if not self.check_running(fail_on_error=(retry == num_retries)):
                    self.log.info('Waiting two seconds for dfuse to start')
                    time.sleep(2)

    def check_running(self, fail_on_error=True):
        """Check if dfuse is running.

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
        self.running_hosts.add(self.hosts)

        self.log.info(
            "Stopping dfuse at %s on %s", self.mount_dir.value, self.running_hosts)

        if self.mount_dir.value is None:
            self.log.info("No dfuse mount directory defined - nothing to stop")
            return

        if not self.running_hosts:
            self.log.info("No hosts running dfuse - nothing to stop")
            return

        error_list = []

        # Loop until all fuseblk mounted devices are unmounted
        counter = 0
        while self.running_hosts and counter < 3:
            # Attempt to kill dfuse on after first unmount fails
            if self.running_hosts and counter > 1:
                kill_command = "pkill dfuse --signal KILL"
                _ = run_remote(self.log, self.running_hosts, kill_command, timeout=30)

            # Attempt to unmount any fuseblk mounted devices after detection
            if self.running_hosts and counter > 0:
                _ = run_remote(self.log, self.running_hosts, self.get_umount_command(counter > 1))
                time.sleep(2)

            # Detect which hosts have fuseblk mounted devices and remove any
            # hosts which no longer have the dfuse mount point mounted
            state = self.check_mount_state(self.running_hosts)
            self.running_hosts.remove(state["unmounted"].union(state["nodirectory"]))

            # Increment the loop counter
            counter += 1

        if self.running_hosts:
            error_list.append(f"Error stopping dfuse on {self.running_hosts}")

        # Remove mount points
        try:
            self.remove_mount_point()
        except CommandFailure as error:
            error_list.append(error)

        # Report any errors
        if error_list:
            raise CommandFailure("\n".join(error_list))


def get_dfuse(test, hosts, namespace=None):
    """Get a new Dfuse instance.

    Args:
        test (Test): the test instance
        hosts (NodeSet): hosts on which to start Dfuse
        namespace (str, optional): dfuse namespace. Defaults to None

    Returns:
        Dfuse: the new dfuse object

    """
    if namespace:
        dfuse = Dfuse(hosts, test.tmp, namespace)
    else:
        dfuse = Dfuse(hosts, test.tmp)
    dfuse.get_params(test)
    return dfuse


def start_dfuse(test, dfuse, pool=None, container=None, **params):
    """Start a Dfuse instance.

    Args:
        test (Test): the test instance
        pool (TestPool, optional): pool to mount. Defaults to None
        container (TestContainer, optional): container to mount. Defaults to None
        params (Object, optional): Dfuse command arguments to update

    """
    # Update dfuse params
    if pool:
        dfuse.set_dfuse_params(pool)
    if container:
        dfuse.set_dfuse_cont_param(container)
    if params:
        dfuse.update_params(**params)
    dfuse.set_dfuse_exports(test.server_managers[0], test.client_log)

    # Start dfuse
    try:
        dfuse.run(bind_cores=test.params.get('cores', dfuse.namespace, None))
    except CommandFailure as error:
        test.log.error(
            "Dfuse command %s failed on hosts %s", str(dfuse), dfuse.hosts, exc_info=error)
        test.fail("Failed to start dfuse")
