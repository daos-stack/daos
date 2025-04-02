"""
  (C) Copyright 2019-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import json
import os
import time

from ClusterShell.NodeSet import NodeSet
from command_utils import ExecutableCommand
from command_utils_base import BasicParameter, FormattedParameter
from exception_utils import CommandFailure
from general_utils import check_file_exists, get_log_file
from run_utils import command_as_user, run_remote


class DfuseCommand(ExecutableCommand):
    """Defines a object representing a dfuse command."""

    def __init__(self, namespace, command, path=""):
        """Create a dfuse Command object."""
        super().__init__(namespace, command, path)

        # dfuse options
        self.mount_dir = BasicParameter(None, position=0)
        self.pool = BasicParameter(None, position=1)
        self.cont = BasicParameter(None, position=2)
        self.sys_name = FormattedParameter("--sys-name {}")
        self.thread_count = FormattedParameter("--thread-count {}")
        self.eq_count = FormattedParameter("--eq-count {}")
        self.foreground = FormattedParameter("--foreground", False)
        self.enable_caching = FormattedParameter("--enable-caching", False)
        self.enable_wb_cache = FormattedParameter("--enable-wb-cache", False)
        self.disable_caching = FormattedParameter("--disable-caching", False)
        self.disable_wb_cache = FormattedParameter("--disable-wb-cache", False)
        self.multi_user = FormattedParameter("--multi-user", False)
        self.read_only = FormattedParameter("--read-only", False)
        self.enable_local_flock = FormattedParameter("--enable-local-flock", False)

    def set_dfuse_exports(self, log_file):
        """Set exports to issue before the dfuse command.

        Args:
            log_file (str): name of the log file to combine with the
                DAOS_TEST_LOG_DIR path with which to assign D_LOG_FILE
        """
        self.env["D_LOG_FILE"] = get_log_file(log_file or "{}_daos.log".format(self.command))


class Dfuse(DfuseCommand):
    """Class defining an object of type DfuseCommand."""

    def __init__(self, hosts, tmp, namespace="/run/dfuse/*", path=""):
        """Create a dfuse object.

        Args:
            hosts (NodeSet): hosts on which to run dfuse
            tmp (str): tmp directory path
            namespace (str): dfuse namespace. Defaults to /run/dfuse/*
            path (str, optional): path to location of command binary file. Defaults to "".

        """
        super().__init__(namespace, "dfuse", path)

        # set params
        self.hosts = hosts.copy()
        self.tmp = tmp

        # hosts where dfuse is currently running
        self._running_hosts = NodeSet()

        # result of last call to _update_mount_state()
        self._mount_state = {}

        # used by stop() to know cleanup is needed
        self.__need_cleanup = False

    def __del__(self):
        """Destruct the object."""
        if self._running_hosts:
            self.log.error('Dfuse object deleted without shutting down')

    def _run_as_owner(self, hosts, command, timeout=120):
        """Run a command as the dfuse mount owner.

        Args:
            hosts (NodeSet): hosts on which to run the command
            command (str): command to run
            timeout (int, optional): number of seconds to wait for the command to complete.
                Defaults to 120 seconds.

        Returns:
            CommandResult: result of the command
        """
        return run_remote(
            self.log, hosts, command_as_user(command, self.run_user), timeout=timeout)

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
        test_result = self._run_as_owner(self.hosts, command)
        check_mounted = test_result.passed_hosts
        if not test_result.passed:
            command = f"grep 'dfuse {self.mount_dir.value}' /proc/mounts"
            grep_result = self._run_as_owner(test_result.failed_hosts, command)
            state["nodirectory"].add(grep_result.failed_hosts)
            # Directory does not exist or is unreadable, but dfuse process is still running
            state["rogue"].add(grep_result.passed_hosts)

        if check_mounted:
            self.log.info("Checking which hosts have dfuse mounted as a fuseblk device")
            command = f"stat -c %T -f {self.mount_dir.value}"
            stat_result = self._run_as_owner(check_mounted, command)
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
            'fusermount3',
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
            result = self._run_as_owner(self._mount_state["nodirectory"], command, timeout=30)
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

        # Try removing cleanly
        command = f"rmdir {self.mount_dir.value}"
        rmdir_result = self._run_as_owner(target_nodes, command, timeout=30)
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

    def run(self, check=True, mount_callback=None):
        # pylint: disable=arguments-differ,arguments-renamed
        """Run the dfuse command.

        Args:
            check (bool): Check if dfuse mounted properly after mount is executed.
            mount_callback (method, optional): method to pass CommandResult to
                after mount. Default simply raises an exception on failure.

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

        # mark the instance as needing cleanup before starting setup
        self.__need_cleanup = True

        # setup the mount point
        self._setup_mount_point()

        # run dfuse command
        result = run_remote(self.log, self.hosts, self.with_exports, timeout=30)
        self._running_hosts.add(result.passed_hosts)
        if mount_callback:
            mount_callback(result)
        elif not result.passed:
            raise CommandFailure(f"dfuse command failed on hosts {result.failed_hosts}")

        if check:
            # Dfuse will block in the command for the mount to complete, even
            # if run in background mode so it should be possible to start using
            # it immediately after the command returns.
            num_retries = 3
            for retry in range(1, num_retries + 1):
                if not self.check_running(fail_on_error=retry == num_retries):
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
                _ = self._run_as_owner(self._running_hosts, kill_command, timeout=30)

            # Try to unmount dfuse on each host, ignoring errors for now
            _ = self._run_as_owner(
                self._running_hosts, self._get_umount_command(force=current_try > 0))
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
        if not self.__need_cleanup:
            return

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

        # Only assume clean if nothing above failed
        self.__need_cleanup = False

    def get_stats(self):
        """Return the I/O stats for the filesystem

        Only works if there is one entry in the client list.
        """

        if len(self.hosts) != 1:
            raise CommandFailure("get_stats only supports one host")

        cmd = f"daos filesystem query --json {self.mount_dir.value}"
        result = run_remote(self.log, self.hosts, cmd)
        if not result.passed:
            raise CommandFailure(f"fs query failed on {result.failed_hosts}")

        data = json.loads("\n".join(result.output[0].stdout))
        if data["status"] != 0 or data["error"] is not None:
            raise CommandFailure("fs query returned bad data.")
        return data["response"]

    def get_log_file_data(self):
        """Return the content of the log file for each clients

        Returns:
            list: lines of the the DFuse log file for each clients

        Raises:
            CommandFailure: on failure to get the DFuse log file

        """
        if not self.env.get("D_LOG_FILE"):
            raise CommandFailure("get_log_file_data needs a DFuse log files to be defined")

        log_file = self.env["D_LOG_FILE"]
        result = run_remote(self.log, self.hosts, f"cat {log_file}")
        if not result.passed:
            raise CommandFailure(f"Log file {log_file} can not be open on {result.failed_hosts}")
        return result


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
        dfuse = Dfuse(hosts, test.tmp, namespace, path=test.bin)
    else:
        dfuse = Dfuse(hosts, test.tmp, path=test.bin)
    dfuse.get_params(test)
    dfuse.set_dfuse_exports(test.client_log)

    # Default mount directory to be test-specific and unique
    if not dfuse.mount_dir.value:
        mount_dir = test.label_generator.get_label(
            os.path.join(os.sep, 'tmp', 'daos_dfuse_' + test.test_id))
        dfuse.update_params(mount_dir=mount_dir)
    return dfuse


def start_dfuse(test, dfuse, pool=None, container=None, **params):
    """Start a Dfuse instance.

    Args:
        test (Test): the test instance
        dfuse (Dfuse): the dfuse instance to start
        pool (TestPool, optional): pool to mount. Defaults to None
        container (TestContainer, optional): container to mount. Defaults to None
        params (Object, optional): Dfuse command arguments to update

    Raises:
        CommandFailure: on failure to start dfuse

    """
    if pool:
        params['pool'] = pool.identifier
    if container:
        params['cont'] = container.identifier
    if params:
        dfuse.update_params(**params)

    # Start dfuse
    try:
        dfuse.bind_cores = test.params.get('cores', dfuse.namespace, None)
        dfuse.run()
        test.register_cleanup(stop_dfuse, test=test, dfuse=dfuse)
    except CommandFailure as error:
        test.log.error("Failed to start dfuse on hosts %s", dfuse.hosts, exc_info=error)
        test.fail("Failed to start dfuse")


def stop_dfuse(test, dfuse):
    """Stop a dfuse instance.

    Args:
        test (Test): the test from which to stop dfuse
        dfuse (Dfuse): the dfuse instance to stop

    Returns:
        list: a list of any errors detected when stopping dfuse

    """
    error_list = []
    try:
        dfuse.stop()
    except (CommandFailure) as error:
        test.log.info("  {}".format(error))
        error_list.append("Error stopping dfuse: {}".format(error))
    return error_list


class VerifyPermsCommand(ExecutableCommand):
    """Class defining an object of type VerifyPermsCommand."""

    def __init__(self, hosts, namespace="/run/verify_perms/*"):
        """Create a VerifyPermsCommand object.

        Args:
            hosts (NodeSet): hosts on which to run the command
            namespace (str): command namespace. Defaults to /run/verify_perms/*

        """
        path = os.path.realpath(os.path.join(os.path.dirname(__file__), '..'))
        super().__init__(namespace, "verify_perms.py", path)

        # verify_perms.py options
        self.path = BasicParameter(None, position=0)
        self.perms = BasicParameter(None, position=1)
        self.owner = FormattedParameter("--owner {}")
        self.group_user = FormattedParameter("--group-user {}")
        self.other_user = FormattedParameter("--other-user {}")
        self.verify_mode = FormattedParameter("--verify-mode {}")
        self.create_type = FormattedParameter("--create-type {}")
        self.no_chmod = FormattedParameter("--no-chmod", False)

        # run options
        self.hosts = hosts.copy()
        self.timeout = 240

        # Most usage requires root permission
        self.run_user = 'root'

    def run(self):
        # pylint: disable=arguments-differ
        """Run the command.

        Raises:
            CommandFailure: If the command fails

        Returns:
            CommandResult: result from run_remote
        """
        self.log.info('Running verify_perms.py on %s', str(self.hosts))
        result = run_remote(self.log, self.hosts, self.with_exports, timeout=self.timeout)
        if not result.passed:
            raise CommandFailure(f'verify_perms.py failed on: {result.failed_hosts}')
        return result


class Pil4dfsDcacheCmd(ExecutableCommand):
    """Defines an object representing a pil4dfs_dcache unit test command."""

    def __init__(self, host, path):
        """Create a Pil4dfsDcacheCmd object.

        Args:
            host (NodeSet): host on which to remotely run the command
            path (str): path of the DAOS install directory
        """
        if len(host) != 1:
            raise ValueError(f"Invalid nodeset '{host}': waiting one client host.")

        test_dir = os.path.join(path, "lib", "daos", "TESTING", "tests")
        super().__init__("/run/pil4dfs_dcache/*", "pil4dfs_dcache", test_dir)

        self._host = host
        self.test_id = BasicParameter(None)

    @property
    def host(self):
        """Get the host on which to remotely run the command via run().

        Returns:
            NodeSet: remote host on which the command will run

        """
        return self._host

    def _run_process(self, raise_exception=None):
        """Run the command remotely as a foreground process.

        Args:
            raise_exception (bool, optional): whether or not to raise an exception if the command
                fails. This overrides the self.exit_status_exception setting if defined.
                Defaults to None.

        Raises:
            CommandFailure: if there is an error running the command

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status
        """
        if raise_exception is None:
            raise_exception = self.exit_status_exception

        # Run pil4dfs_dcache remotely
        result = run_remote(self.log, self._host, self.with_exports, timeout=None)
        if raise_exception and not result.passed:
            raise CommandFailure(f"Error running pil4dfs_dcache on host: {result.failed_hosts}\n")
        return result
