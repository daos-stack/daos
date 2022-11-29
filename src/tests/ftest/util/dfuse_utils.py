"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
from ClusterShell.NodeSet import NodeSet

from command_utils_base import FormattedParameter
from exception_utils import CommandFailure
from command_utils import ExecutableCommand
from general_utils import check_file_exists, get_log_file
from run_utils import run_remote, command_as_user


class DfuseCommand(ExecutableCommand):
    """Defines a object representing a dfuse command."""

    def __init__(self, namespace, command):
        """Create a dfuse Command object."""
        super().__init__(namespace, command)

        # dfuse options
        self.pool = FormattedParameter("--pool {}")
        self.cont = FormattedParameter("--container {}")
        self.mount_dir = FormattedParameter("--mountpoint {}")
        self.sys_name = FormattedParameter("--sys-name {}")
        self.thread_count = FormattedParameter("--thread-count {}")
        self.singlethreaded = FormattedParameter("--singlethread", False)
        self.foreground = FormattedParameter("--foreground", False)
        self.enable_caching = FormattedParameter("--enable-caching", False)
        self.enable_wb_cache = FormattedParameter("--enable-wb-cache", False)
        self.disable_caching = FormattedParameter("--disable-caching", False)
        self.disable_wb_cache = FormattedParameter("--disable-wb-cache", False)

    def set_dfuse_exports(self, log_file):
        """Set exports to issue before the dfuse command.

        Args:
            log_file (str): name of the log file to combine with the
                DAOS_TEST_LOG_DIR path with which to assign D_LOG_FILE
        """
        self.env["D_LOG_FILE"] = get_log_file(log_file or "{}_daos.log".format(self.command))


class Dfuse(DfuseCommand):
    """Class defining an object of type DfuseCommand."""

    def __init__(self, hosts, tmp):
        """Create a dfuse object.

        Args:
            hosts (NodeSet): hosts on which to run dfuse
            tmp (str): tmp directory path

        """
        super().__init__("/run/dfuse/*", "dfuse")

        # set params
        self.hosts = hosts.copy()
        self.tmp = tmp

        # hosts where dfuse is currently running
        self._running_hosts = NodeSet()

        # result of last call to _update_mount_state()
        self._mount_state = {}

        # which fusermount command to use for unmount
        self._fusermount_cmd = ""

    def __del__(self):
        """Destruct the object."""
        if self._running_hosts:
            self.log.error('Dfuse object deleted without shutting down')

    def _update_mount_state(self):
        """Update the mount state for each host."""
        state = {
            "mounted": NodeSet(),
            "unmounted": NodeSet(),
            "nodirectory": NodeSet(),
            "rogue": NodeSet()
        }

        self.log.info("Checking which hosts have the mount point directory created")
        command = f"test -d {self.mount_dir.value} -a ! -L {self.mount_dir.value}"
        test_result = run_remote(self.log, self.hosts, command)
        check_mounted = test_result.passed_hosts
        if not test_result.passed:
            command = f"grep 'dfuse {self.mount_dir.value}' /proc/mounts"
            grep_result = run_remote(self.log, test_result.failed_hosts, command)
            state["nodirectory"].add(grep_result.failed_hosts)
            # Directory does not exist or is unreadable, but dfuse process is still running
            state["rogue"].add(grep_result.passed_hosts)

        if check_mounted:
            self.log.info("Checking which hosts have dfuse mounted as a fuseblk device")
            command = f"stat -c %T -f {self.mount_dir.value}"
            stat_result = run_remote(self.log, check_mounted, command)
            for data in stat_result.output:
                if data.returncode == 0 and 'fuseblk' in '\n'.join(data.stdout):
                    state["mounted"].add(data.hosts)
                else:
                    state["unmounted"].add(data.hosts)

        # Log the state of each host
        for _state, _hosts in state.items():
            self.log.debug("%s: %s", _state, _hosts)

        # Cache the state
        self._mount_state = state

        # Update the running hosts
        self._running_hosts = self._mount_state["mounted"].union(self._mount_state["rogue"])

    def _get_umount_command(self, force=False):
        """Get the command to umount the dfuse mount point.

        Args:
            force (bool, optional): whether to force the umount with a lazy
                unmount. Defaults to False.

        Returns:
            str: the dfuse umount command

        """
        return ' '.join(filter(None, [
            self._fusermount_cmd,
            '-u',
            '-z' if force else None,
            self.mount_dir.value
        ]))

    def _setup_mount_point(self):
        """Setup the dfuse mount point.

        Raises:
            CommandFailure: In case of error

        """
        # Raise exception if mount point not specified
        if self.mount_dir.value is None:
            raise CommandFailure("Mount point not specified. Check test yaml file")

        # Unmount dfuse if already running
        self.unmount()
        if self._running_hosts:
            raise CommandFailure(f"Error stopping dfuse on {self._running_hosts}")

        self.log.info("Creating dfuse mount directory")
        if self._mount_state["nodirectory"]:
            command = f"mkdir -p {self.mount_dir.value}"
            result = run_remote(self.log, self._mount_state["nodirectory"], command, timeout=30)
            if not result.passed:
                raise CommandFailure(
                    f"Error creating the {self.mount_dir.value} dfuse mount point "
                    f"on the following hosts: {result.failed_hosts}")

    def _remove_mount_point(self):
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

        # Try removing with rmdir
        command = f"rmdir {self.mount_dir.value}"
        rmdir_result = run_remote(self.log, target_nodes, command, timeout=30)
        if rmdir_result.passed:
            return

        # Try removing as root for good measure
        command = command_as_user(f"rm -rf {self.mount_dir.value}", "root")
        rm_result = run_remote(self.log, rmdir_result.failed_hosts, command, timeout=30)
        if not rm_result.passed:
            raise CommandFailure(
                f"Error removing the {self.mount_dir.value} dfuse mount point with rm on "
                f"the following hosts: {rm_result.failed_hosts}")

        # rm -rf worked but rmdir failed
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

        # Determine which fusermount command to use before mounting
        if not self._fusermount_cmd:
            self.log.info('Check which fusermount command to use')
            for fusermount in ('fusermount3', 'fusermount'):
                if run_remote(self.log, self.hosts, f'{fusermount} --version').passed:
                    self._fusermount_cmd = fusermount
                    break
            if not self._fusermount_cmd:
                raise CommandFailure(f'Failed to get fusermount command on: {self.hosts}')

        # setup the mount point
        self._setup_mount_point()

        # run dfuse command
        cmd = self.env.to_export_str()
        if bind_cores:
            cmd += 'taskset -c {} '.format(bind_cores)
        cmd += str(self)

        result = run_remote(self.log, self.hosts, cmd, timeout=30)
        self._running_hosts.add(result.passed_hosts)
        if not result.passed:
            raise CommandFailure(
                f"dfuse command failed on hosts {result.failed_hosts}")

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
        self._update_mount_state()
        if self._mount_state["unmounted"] or self._mount_state["nodirectory"]:
            self.log.error(
                "dfuse not running on %s",
                str(self._mount_state["unmounted"].union(self._mount_state["nodirectory"])))
            if fail_on_error:
                raise CommandFailure("dfuse not running")
            return False
        if self._mount_state["rogue"]:
            self.log.error("rogue dfuse processes on %s", str(self._mount_state["rogue"]))
            if fail_on_error:
                raise CommandFailure("rogue dfuse processes detected")
            return False
        return True

    def unmount(self, tries=2):
        """Unmount dfuse.

        Args:
            tries (int, optional): number of times to try unmount. Defaults to 2

        """
        self._update_mount_state()

        for current_try in range(tries):
            if not self._running_hosts:
                return

            # Forcibly kill dfuse after the first unmount fails
            if current_try > 0:
                kill_command = "pkill dfuse --signal KILL"
                _ = run_remote(self.log, self._running_hosts, kill_command, timeout=30)

            # Try to unmount dfuse on each host, ignoring errors for now
            _ = run_remote(
                self.log, self._running_hosts, self._get_umount_command(force=(current_try > 0)))
            time.sleep(2)

            self._update_mount_state()

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
        self.log.info("Stopping dfuse at %s on %s", self.mount_dir.value, self.hosts)

        if self.mount_dir.value is None:
            self.log.info("No dfuse mount directory defined - nothing to stop")
            return

        if not self.hosts:
            self.log.info("No hosts running dfuse - nothing to stop")
            return

        self.unmount()

        error_list = []
        if self._running_hosts:
            error_list.append(f"Error stopping dfuse on {self._running_hosts}")

        # Remove mount points
        try:
            self._remove_mount_point()
        except CommandFailure as error:
            error_list.append(str(error))

        # Report any errors
        if error_list:
            raise CommandFailure("\n".join(error_list))


def get_dfuse(test, hosts):
    """Get a new Dfuse instance.

    Args:
        test (Test): the test instance
        hosts (NodeSet): hosts on which to start Dfuse

    Returns:
        Dfuse: the new dfuse object

    """
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
    if pool:
        params['pool'] = pool.identifier
    if container:
        params['cont'] = container.uuid
    if params:
        dfuse.update_params(**params)
    dfuse.set_dfuse_exports(test.client_log)

    # Start dfuse
    try:
        dfuse.run(bind_cores=test.params.get('cores', dfuse.namespace, None))
    except CommandFailure as error:
        test.log.error("Failed to start dfuse on hosts %s", dfuse.hosts, exc_info=error)
        test.fail("Failed to start dfuse")
