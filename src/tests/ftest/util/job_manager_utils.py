"""
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re
import time
# pylint: disable=too-many-lines
from shutil import which

from ClusterShell.NodeSet import NodeSet
from command_utils import ExecutableCommand, SystemctlCommand
from command_utils_base import BasicParameter, EnvironmentVariables, FormattedParameter
from env_modules import load_mpi
from exception_utils import CommandFailure, MPILoadError
from general_utils import get_job_manager_class, get_journalctl_command, journalctl_time
from run_utils import run_remote, stop_processes
from write_host_file import write_host_file


def get_job_manager(test, class_name=None, job=None, subprocess=None, mpi_type=None, timeout=None,
                    namespace="/run/job_manager/*", class_name_default="Mpirun"):
    """Get a JobManager object.

    Create a JobManager class using either:
        - the provided arguments
        - the test yaml arguments (if no arguments are provided)
        - default values (if no arguments are provided and there are no test yaml entries)

    Use this method to ensure the cleanup of processes started by JobManager.run() are handled in
    the correct order in the test tearDown() method.

    Args:
        test (Test): avocado Test object
        class_name (str, optional): JobManager class name. Defaults to None.
        job (ExecutableCommand, optional): command object to manage. Defaults to None.
        subprocess (bool, optional): whether the command is run as a subprocess. Defaults to False.
        mpi_type (str, optional): MPI type to use with the Mpirun class only. Defaults to "openmpi".
        timeout (int, optional): job manager timeout. Defaults to None.
        namespace (str, optional): location of yaml parameters used to define unset inputs. Defaults
            to "/run/job_manager/*".
        class_name_default (str, optional): default class_name to use when. Defaults to "Mpirun".

    Raises:
        DaosTestError: if an invalid JobManager class name is specified.

    Returns:
        JobManager: a JobManager class, e.g. Orterun, Mpirun, Srun, etc.
    """
    if class_name is None:
        class_name = test.params.get("class_name", namespace, default=class_name_default)
    if subprocess is None:
        subprocess = test.params.get("subprocess", namespace, default=False)
    if mpi_type is None:
        mpi_type_default = "mpich" if class_name == "Mpirun" else "openmpi"
        mpi_type = test.params.get("mpi_type", namespace, default=mpi_type_default)
    if timeout is None:
        timeout = test.params.get(
            test.get_test_name(), namespace.replace("*", "manager_timeout/*"), None)
        if timeout is None:
            timeout = test.params.get("timeout", namespace, None)

    # Setup a job manager command for running the test command
    job_manager = get_job_manager_class(class_name, job, subprocess, mpi_type)
    job_manager.get_params(test)
    job_manager.timeout = timeout
    if mpi_type == "openmpi" and hasattr(job_manager, "tmpdir_base"):
        job_manager.tmpdir_base.update(test.test_dir, "tmpdir_base")
    job_manager.register_cleanup_method = test.register_cleanup
    return job_manager


def stop_job_manager(job_manager):
    """Stop the job manager being run by the test.

    Args:
        job_manager (JobManager): the job manager to stop

    Returns:
        list: a list of any errors detected when stopping the job manager
    """
    error_list = []
    job_manager.log.info("Stopping %s job manager: %s", job_manager.command, job_manager)
    try:
        job_manager.stop()
    except Exception as error:      # pylint: disable=broad-except
        job_manager.log.error("Error stopping %s job manager: %s", job_manager.command, error)
        error_list.append(f"Error detected stopping {job_manager.command} job manager")
    return error_list


class JobManager(ExecutableCommand):
    """A class for commands with parameters that manage other commands."""

    def __init__(self, namespace, command, job, path="", subprocess=False):
        """Create a JobManager object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            job (ExecutableCommand): command object to manage.
            path (str, optional): path to location of command binary file.
                Defaults to "".
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        super().__init__(namespace, command, path, subprocess)
        self.job = job
        self._hosts = NodeSet()
        self.register_cleanup_method = None

    @property
    def hosts(self):
        """Get the hosts associated with this command.

        Returns:
            NodeSet: hosts used to run the job

        """
        return self._hosts

    @property
    def job(self):
        """Get the JobManager job.

        Returns:
            ExecutableCommand: the command managed by this class

        """
        return self._job

    @job.setter
    def job(self, value):
        """Set the JobManager job.

        Args:
            value (ExecutableCommand): the command to be managed by this class
        """
        self._job = value
        if (self._job and hasattr(self._job, "check_results_list")
                and self._job.check_results_list):
            self.check_results_list.extend(self._job.check_results_list)

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        commands = [super().__str__(), str(self.job)]
        return " ".join(commands)

    def check_subprocess_status(self, sub_process):
        """Verify command status when called in a subprocess.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command

        Returns:
            bool: whether or not the command progress has been detected

        """
        return self.job.check_subprocess_status(sub_process)

    def assign_hosts(self, hosts, path=None, slots=None, hostfile=True):
        """Assign the hosts to use with the command.

        Set the appropriate command line parameter with the specified value.

        Args:
            hosts (NodeSet): hosts to specify on the command line
            path (str, optional): path to use when specifying the hosts through
                a hostfile. Defaults to None.
            slots (int, optional): number of slots per host to specify in the
                optional hostfile. Defaults to None.
            hostfile (bool, optional): whether or not to also update any host related command
                parameters to keep them in sync with the hosts. Defaults to True.
        """
        # pylint: disable=unused-argument
        if not isinstance(hosts, NodeSet):
            raise ValueError(f'Expected hosts to be type {type(NodeSet)} but got {type(hosts)}')
        self._hosts = hosts.copy()

    def _setup_hostfile(self, path=None, slots=None, hostfile=True):
        """Setup the hostfile to use with the command.

        Args:
            path (str, optional): path to use when specifying the hosts through
                a hostfile. Defaults to None.
            slots (int, optional): number of slots per host to specify in the
                optional hostfile. Defaults to None.
            hostfile (bool, optional): whether or not to also update any host related command
                parameters to keep them in sync with the hosts. Defaults to True.

        Returns:
            str: the full path of the written hostfile; None if one is not written
        """
        if not hostfile:
            return None
        kwargs = {"hosts": self._hosts, "slots": slots}
        if path is not None:
            kwargs["path"] = path
        return write_host_file(**kwargs)

    def assign_processes(self, processes):
        """Assign the number of processes.

        Set the appropriate command line parameter with the specified value.

        Args:
            processes (int): number of processes
        """

    def assign_environment(self, env_vars, append=False):
        """Assign or add environment variables to the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to use
                assign or add to the command
            append (bool): whether to assign (False) or append (True) the
                specified environment variables
        """

    def assign_environment_default(self, env_vars):
        """Assign the default environment variables for the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to
                assign as the default
        """

    def get_subprocess_state(self, message=None):
        """Display the state of the subprocess.

        Args:
            message (str, optional): additional text to include in output.
                Defaults to None.

        Returns:
            list: a list of states for the process found. If the local job
                manager command is running its state will be the first in the
                list. Additional states in the list can typically indicate that
                remote processes were also found to be active.  Active remote
                processes will be indicated by a 'R' state at the end of the
                list.

        """
        # Get/display the state of the local job manager process
        state = super().get_subprocess_state(message)
        if self._process is not None and self._hosts:
            # Determine if the status of the remote job processes on each host
            remote_state = self._get_remote_process_state(message)
            if remote_state:
                # Add a running state to the list of process states if any
                # remote process was found to be active.
                if not state:
                    state = ["?"]
                state.append(remote_state)
        return state

    def _get_remote_process_state(self, message=None):
        """Display the state of the processes running on remote hosts.

        Args:
            message (str, optional): additional text to include in output.
                Defaults to None.

        Returns:
            str: a "R" if any remote processes are found to be active otherwise
                None.

        """
        # Display the status of the remote job processes on each host
        command = "/usr/bin/pgrep -a {}".format(self.job.command_regex)
        self.log.debug(
            "%s processes still running remotely%s:", self.command,
            " {}".format(message) if message else "")
        result = run_remote(self.log, self._hosts, command, timeout=10)
        # Return "R" if processes were found running on any hosts
        return "R" if result.passed_hosts else None

    def run(self, raise_exception=None):
        """Run the command.

        Args:
            raise_exception (bool, optional): whether or not to raise an exception if the command
                fails. This overrides the self.exit_status_exception
                setting if defined. Defaults to None.

        Raises:
            CommandFailure: if there is an error running the command

        Returns:
            CmdResult: result of the command
        """
        if callable(self.register_cleanup_method):
            # Stop any running processes started by this job manager when the test completes
            # pylint: disable=not-callable
            self.register_cleanup_method(stop_job_manager, job_manager=self)
        return super().run(raise_exception)

    def stop(self):
        """Stop the subprocess command and kill any job processes running on hosts.

        Raises:
            CommandFailure: if unable to stop

        """
        super().stop()
        self.kill()

    def kill(self):
        """Forcibly terminate any job processes running on hosts."""
        if not self.job:
            return
        regex = self.job.command_regex
        if self.job.full_command_regex:
            regex = f"'{str(self.job)}'"
        detected, running = stop_processes(
            self.log, self._hosts, regex, full_command=self.job.full_command_regex)
        if not detected:
            self.log.info(
                "No remote %s processes killed on %s (none found), done.", regex, self._hosts)
        elif running:
            self.log.info(
                "***Unable to kill remote %s process on %s! Please investigate/report.***",
                regex, running)
        else:
            self.log.info(
                "***At least one remote %s process needed to be killed on %s! Please investigate/"
                "report.***", regex, detected)


class Orterun(JobManager):
    """A class for the orterun job manager command."""

    def __init__(self, job, subprocess=False, mpi_type="openmpi"):
        """Create a Orterun object.

        Args:
            job (ExecutableCommand): command object to manage.
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        if not load_mpi(mpi_type):
            raise MPILoadError(mpi_type)

        path = os.path.dirname(which("orterun"))
        super().__init__("/run/orterun/*", "orterun", job, path, subprocess)

        # Default mca values to avoid queue pair errors
        mca_default = {
            "btl_openib_warn_default_gid_prefix": "0",
            "btl": "tcp,self",
            "oob": "tcp",
            "coll": "^hcoll",
            "pml": "ob1",
            "btl_tcp_if_include": "eth0",
        }

        self.hostfile = FormattedParameter("--hostfile {}", None)
        self.processes = FormattedParameter("--np {}", 1)
        self.display_map = FormattedParameter("--display-map", False)
        self.map_by = FormattedParameter("--map-by {}", "node")
        self.export = FormattedParameter("-x {}", None)
        self.enable_recovery = FormattedParameter("--enable-recovery", True)
        self.report_uri = FormattedParameter("--report-uri {}", None)
        self.allow_run_as_root = FormattedParameter("--allow-run-as-root", None)
        self.mca = FormattedParameter("--mca {}", mca_default)
        self.pprnode = FormattedParameter("--map-by ppr:{}:node", None)
        self.tag_output = FormattedParameter("--tag-output", True)
        self.ompi_server = FormattedParameter("--ompi-server {}", None)
        self.working_dir = FormattedParameter("-wdir {}", None)
        self.tmpdir_base = FormattedParameter("--mca orte_tmpdir_base {}", None)
        self.bind_to = FormattedParameter("--bind-to {}", None)
        self.mpi_type = mpi_type

    def assign_hosts(self, hosts, path=None, slots=None, hostfile=True):
        """Assign the hosts to use with the command (--hostfile).

        Args:
            hosts (NodeSet): hosts to specify in the hostfile
            path (str, optional): hostfile path. Defaults to None.
            slots (int, optional): number of slots per host to specify in the
                hostfile. Defaults to None.
            hostfile (bool, optional): whether or not to also update any host related command
                parameters to keep them in sync with the hosts. Defaults to True.
        """
        super().assign_hosts(hosts, path, slots, hostfile)
        self.hostfile.value = self._setup_hostfile(path, slots, hostfile)

    def assign_processes(self, processes):
        """Assign the number of processes (-np).

        Args:
            processes (int): number of processes
        """
        self.processes.value = processes

    def assign_environment(self, env_vars, append=False):
        """Assign or add environment variables to the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to use
                assign or add to the command
            append (bool): whether to assign (False) or append (True) the
                specified environment variables
        """
        if append and self.export.value is not None:
            # Convert the current list of environmental variable assignments
            # into an EnvironmentVariables (dict) object.  Then update the
            # dictionary keys with the specified values or add new key value
            # pairs to the dictionary.  Finally convert the updated dictionary
            # back to a list for the parameter assignment.
            original = EnvironmentVariables.from_list(self.export.value)
            original.update(env_vars)
            self.export.value = original.to_list()
        else:
            # Overwrite the environmental variable assignment
            self.export.value = env_vars.to_list()

    def assign_environment_default(self, env_vars):
        """Assign the default environment variables for the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to
                assign as the default
        """
        self.export.update_default(env_vars.to_list())

    def run(self, raise_exception=None):
        """Run the orterun command.

        Args:
            raise_exception (bool, optional): whether or not to raise an exception if the command
                fails. This overrides the self.exit_status_exception
                setting if defined. Defaults to None.

        Raises:
            CommandFailure: if there is an error running the command

        """
        if not load_mpi(self.mpi_type):
            raise MPILoadError(self.mpi_type)

        return super().run(raise_exception)


class Mpirun(JobManager):
    """A class for the mpirun job manager command."""

    def __init__(self, job, subprocess=False, mpi_type="openmpi"):
        """Create a Mpirun object.

        Args:
            job (ExecutableCommand): command object to manage.
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        if not load_mpi(mpi_type):
            raise MPILoadError(mpi_type)

        path = os.path.dirname(which("mpirun"))
        super().__init__("/run/mpirun/*", "mpirun", job, path, subprocess)

        mca_default = None
        if mpi_type == "openmpi":
            # Default mca values to avoid queue pair errors w/ OpenMPI
            mca_default = {
                "btl_openib_warn_default_gid_prefix": "0",
                "btl": "tcp,self",
                "oob": "tcp",
                "coll": "^hcoll",
                "pml": "ob1",
                "btl_tcp_if_include": "eth0",
            }

        self.hostfile = FormattedParameter("-hostfile {}", None)
        self.processes = FormattedParameter("-np {}", 1)
        self.ppn = FormattedParameter("-ppn {}", None)
        self.envlist = FormattedParameter("-envlist {}", None)
        if mpi_type == "openmpi":
            self.genv = FormattedParameter("-x {}", None)
        else:
            self.genv = FormattedParameter("-genv {}", None)
        self.mca = FormattedParameter("--mca {}", mca_default)
        self.working_dir = FormattedParameter("-wdir {}", None)
        self.tmpdir_base = FormattedParameter("--mca orte_tmpdir_base {}", None)
        self.args = BasicParameter(None, None)
        self.mpi_type = mpi_type
        self.hostlist = FormattedParameter("-hosts {}", None)

    def assign_hosts(self, hosts, path=None, slots=None, hostfile=True):
        """Assign the hosts to use with the command (-f).

        Args:
            hosts (NodeSet): hosts to specify in the hostfile
            path (str, optional): hostfile path. Defaults to None.
            slots (int, optional): number of slots per host to specify in the
                hostfile. Defaults to None.
            hostfile (bool, optional): whether or not to also update any host related command
                parameters to keep them in sync with the hosts. Defaults to True.
        """
        super().assign_hosts(hosts, path, slots, hostfile)
        self.hostfile.value = self._setup_hostfile(path, slots, hostfile)

    def assign_processes(self, processes=None, ppn=None):
        """Assign the number of processes (-np) and processes per node (-ppn).

        Args:
            processes (int, optional): number of processes. Defaults to None.
                if not specified, auto-calculated from ppn.
            ppn (int, optional): number of processes per node. Defaults to None.
        """
        if ppn is not None and processes is None:
            processes = ppn * len(self._hosts)
        self.processes.update(processes, "mpirun.np")
        self.ppn.update(ppn, "mpirun.ppn")

    def assign_environment(self, env_vars, append=False):
        """Assign or add environment variables to the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to use
                assign or add to the command
            append (bool): whether to assign (False) or append (True) the
                specified environment variables
        """
        # Pass the environment variables via the process.run method env argument
        if append and self.env is not None:
            # Update the existing dictionary with the new values
            self.genv.update(env_vars.to_list())
        else:
            # Overwrite/create the dictionary of environment variables
            self.genv.update((EnvironmentVariables(env_vars)).to_list())

    def assign_environment_default(self, env_vars):
        """Assign the default environment variables for the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to
                assign as the default
        """
        self.genv.update_default(env_vars.to_list())

    def run(self, raise_exception=None):
        """Run the mpirun command.

        Args:
            raise_exception (bool, optional): whether or not to raise an exception if the command
                fails. This overrides the self.exit_status_exception
                setting if defined. Defaults to None.

        Raises:
            CommandFailure: if there is an error running the command

        """
        if not load_mpi(self.mpi_type):
            raise MPILoadError(self.mpi_type)

        return super().run(raise_exception)


class Srun(JobManager):
    """A class for the srun job manager command."""

    def __init__(self, job, path="", subprocess=False):
        """Create a Srun object.

        Args:
            job (ExecutableCommand): command object to manage.
            path (str, optional): path to location of command binary file.
                Defaults to "".
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        super().__init__("/run/srun/*", "srun", job, path, subprocess)

        self.label = FormattedParameter("--label", True)
        self.mpi = FormattedParameter("--mpi={}", "pmi2")
        self.export = FormattedParameter("--export={}", "ALL")
        self.ntasks = FormattedParameter("--ntasks={}", None)
        self.distribution = FormattedParameter("--distribution={}", None)
        self.nodefile = FormattedParameter("--nodefile={}", None)
        self.nodelist = FormattedParameter("--nodelist={}", None)
        self.ntasks_per_node = FormattedParameter("--ntasks-per-node={}", None)
        self.nodes = FormattedParameter("--nodes={}", None)
        self.reservation = FormattedParameter("--reservation={}", None)
        self.partition = FormattedParameter("--partition={}", None)
        self.output = FormattedParameter("--output={}", None)

    def assign_hosts(self, hosts, path=None, slots=None, hostfile=True):
        """Assign the hosts to use with the command (-f).

        Args:
            hosts (NodeSet): hosts to specify in the hostfile
            path (str, optional): hostfile path. Defaults to None.
            slots (int, optional): number of slots per host to specify in the
                hostfile. Defaults to None.
            hostfile (bool, optional): whether or not to also update any host related command
                parameters to keep them in sync with the hosts. Defaults to True.
        """
        super().assign_hosts(hosts, path, slots, hostfile)
        self.nodefile.value = self._setup_hostfile(path, slots, hostfile)
        self.ntasks_per_node.value = slots

    def assign_processes(self, processes):
        """Assign the number of processes per node.

        Args:
            processes (int): number of processes per node
        """
        self.ntasks.value = processes
        self.distribution.value = "cyclic"

    def assign_environment(self, env_vars, append=False):
        """Assign or add environment variables to the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to use
                assign or add to the command
            append (bool): whether to assign (False) or append (True) the
                specified environment variables
        """
        if append and self.export.value is not None:
            # Convert the current list of environmental variable assignments
            # into an EnvironmentVariables (dict) object.  Then update the
            # dictionary keys with the specified values or add new key value
            # pairs to the dictionary.  Finally convert the updated dictionary
            # back to a string for the parameter assignment.
            original = EnvironmentVariables.from_list(self.export.value.split(","))
            original.update(env_vars)
            self.export.value = ",".join(original.to_list())
        else:
            # Overwrite the environmental variable assignment
            self.export.value = ",".join(env_vars.to_list())

    def assign_environment_default(self, env_vars):
        """Assign the default environment variables for the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to
                assign as the default
        """
        self.export.update_default(env_vars.to_list())


class Systemctl(JobManager):
    # pylint: disable=too-many-public-methods
    """A class for the systemctl job manager command."""

    def __init__(self, job):
        """Create a Orterun object.

        Args:
            job (SubProcessCommand): command object to manage.
        """
        super().__init__("/run/systemctl/*", "systemd", job)
        self.job = job
        self._systemctl = SystemctlCommand()
        self._systemctl.service.value = self.job.service_name

        self.timestamps = {
            "enable": None,
            "disable": None,
            "start": None,
            "running": None,
            "verified": None,
            "stop": None,
            "restart": None,
        }

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        return self._systemctl.__str__()

    def run(self, raise_exception=None):
        """Start the job's service via the systemctl command.

        Enable the service, start the service, and report the status of the
        service.  If an error occurs with any of these commands also display
        the journalctl output for the service.

        Args:
            raise_exception (bool, optional): whether or not to raise an exception if the command
                fails. This overrides the self.exit_status_exception
                setting if defined. Defaults to None.

        Raises:
            CommandFailure: if unable to enable or start the service

        Returns:
            dict: a dictionary of return codes keys and accompanying NodeSet
                values indicating which hosts yielded the return code.

        """
        if raise_exception is None:
            raise_exception = self.exit_status_exception

        # Start the daos_server.service
        self.service_enable()
        result = self.service_start()

        # Determine if the command has launched correctly using its
        # check_subprocess_status() method.
        if not self.check_subprocess_status(None):
            msg = "Command '{}' did not launch correctly".format(self)
            self.log.error(msg)
            if raise_exception:
                raise CommandFailure(msg)

        return result

    def stop(self):
        """Stop the job's service via the systemctl command.

        Stop the service, disable the service, and report the status of the
        service.  If an error occurs with any of these commands also display
        the journalctl output for the service.

        Raises:
            CommandFailure: if unable to stop or disable the service

        Returns:
            dict: a dictionary of return codes keys and accompanying NodeSet
                values indicating which hosts yielded the return code.

        """
        self.service_stop()
        return self.service_disable()

    def wait(self):
        """Wait for the sub process to complete."""
        raise NotImplementedError()

    def kill(self):
        """Forcibly terminate any job processes running on hosts."""
        try:
            self.stop()
        except CommandFailure as error:
            self.log.info(
                "Error stopping/disabling %s: %s", self.job.service_name, error)
        super().kill()

    def check_subprocess_status(self, sub_process):
        """Verify command status when called in a subprocess.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command

        Returns:
            bool: whether or not the command progress has been detected

        """
        return self.check_logs(
            self.job.pattern, self.timestamps["start"], None,
            self.job.pattern_count, self.job.pattern_timeout.value)

    def assign_environment(self, env_vars, append=False):
        """Assign or add environment variables to the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to use
                assign or add to the command
            append (bool): whether to assign (False) or append (True) the
                specified environment variables
        """

    def assign_environment_default(self, env_vars):
        """Assign the default environment variables for the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to
                assign as the default
        """

    def get_subprocess_state(self, message=None):
        """Display the state of the subprocess.

        Args:
            message (str, optional): additional text to include in output.
                Defaults to None.

        Returns:
            list: a list of states for the process found. Any active remote
                processes will be indicated by a 'R' state at the end of the
                list.

        """
        state = None
        remote_state = self._get_remote_process_state(message)
        if remote_state:
            state = [remote_state]
        return state

    def _run_unit_command(self, command):
        """Run the systemctl command.

        Args:
            command (str): systemctl unit command

        Raises:
            CommandFailure: if there is an issue running the command

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self._systemctl.unit_command.value = command
        self.timestamps[command] = journalctl_time()
        cmd = str(self)
        result = run_remote(
            self.log, self._hosts, cmd, verbose=self.verbose, timeout=self.timeout)
        if result.timeout:
            raise CommandFailure(
                "Timeout detected running '{}' with a {}s timeout on {}".format(
                    cmd, self.timeout, result.timeout_hosts))
        if not result.passed:
            raise CommandFailure(
                "Error occurred running '{}' on {}".format(cmd, result.failed_hosts))
        return result

    def _report_unit_command(self, command):
        """Run the systemctl command and report the log data on an error.

        Args:
            command (str): systemctl unit command

        Raises:
            CommandFailure: if there is an issue running the command

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        try:
            return self._run_unit_command(command)
        except CommandFailure as error:
            self.log.info(error)
            command = get_journalctl_command(
                self.timestamps[command], units=self._systemctl.service.value)
            self.display_log_data(self.get_log_data(self._hosts, command))
            raise CommandFailure(error) from error

    def service_enable(self):
        """Enable the job's service via the systemctl command.

        Raises:
            CommandFailure: if unable to enable

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        return self._report_unit_command("enable")

    def service_disable(self):
        """Disable the job's service via the systemctl command.

        Raises:
            CommandFailure: if unable to disable

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        return self._report_unit_command("disable")

    def service_start(self):
        """Start the job's service via the systemctl command.

        Raises:
            CommandFailure: if unable to start

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        return self._report_unit_command("start")

    def service_stop(self):
        """Stop the job's service via the systemctl command.

        Raises:
            CommandFailure: if unable to stop

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        return self._report_unit_command("stop")

    def service_status(self):
        """Get the status of the job's service via the systemctl command.

        Raises:
            CommandFailure: if unable to get the status

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        return self._report_unit_command("status")

    def service_running(self):
        """Determine if the job's service is active via the systemctl command.

        The 'systemctl is-active <service>' command will return a string
        indicating one of the following states:
            active, inactive, activating, deactivating, failed, unknown
        If the <service> is "active" or "activating" return True.

        Returns:
            bool: True id the service is running, False otherwise

        """
        status = True
        states = {}
        valid_states = ["active", "activating"]
        self._systemctl.unit_command.value = "is-active"
        result = run_remote(self.log, self._hosts, str(self), verbose=False, timeout=self.timeout)
        if result.timeout:
            states["timeout"] = result.timeout_hosts
        for data in result.output:
            if data.timeout:
                continue
            output = data.stdout[-1]
            if output not in states:
                states[output] = NodeSet()
            states[output].add(data.hosts)
            status &= output in valid_states
        data = ["=".join([key, str(states[key])]) for key in sorted(states)]
        self.log.info(
            "  Detected %s states: %s",
            self._systemctl.service.value, ", ".join(data))
        return status

    def get_log_data(self, hosts, command, timeout=60):
        """Gather log output for the command running on each host.

        Note (from journalctl man page):
            Date specifications should be of the format "2012-10-30 18:17:16".
            If the time part is omitted, "00:00:00" is assumed. If only the
            seconds component is omitted, ":00" is assumed. If the date
            component is omitted, the current day is assumed. Alternatively the
            strings "yesterday", "today", "tomorrow" are understood, which refer
            to 00:00:00 of the day before the current day, the current day, or
            the day after the current day, respectively.  "now" refers to the
            current time. Finally, relative times may be specified, prefixed
            with "-" or "+", referring to times before or after the current
            time, respectively.

        Args:
            hosts (NodeSet): hosts from which to gather log data.
            command (str): journalctl command to issue to produce the log data.
            timeout (int, optional): timeout for issuing the command. Defaults
                to 60 seconds.

        Returns:
            list: a list of dictionaries including:
                "hosts": <NodeSet() of hosts with this data>
                "data": <journalctl output>

        """
        self.log.info("Gathering log data on %s: %s", str(hosts), command)

        # Gather the log information per host
        result = run_remote(self.log, hosts, command, verbose=False, timeout=timeout)

        # Determine if the command completed successfully without a timeout
        if not result.passed:
            self.log.info('  Errors detected running "%s":', command)
        if result.timeout:
            self.log.info(
                "    %s: timeout detected after %s seconds", str(result.timeout_hosts), timeout)

        # Display/return the command output
        log_data = []
        for data in result.output:
            if data.returncode == 0:
                # Add the successful output from each node to the dictionary
                log_data.append(
                    {"hosts": data.hosts, "data": data.stdout})
            else:
                # Display all of the results in the case of an error
                if len(data.stdout) > 1:
                    self.log.info(
                        "    %s: rc=%s, output:",
                        str(data.hosts), data.returncode)
                    for line in data.stdout:
                        self.log.info("      %s", line)
                else:
                    self.log.info(
                        "    %s: rc=%s, output: %s",
                        str(data.hosts), data.returncode,
                        data.stdout[0])

        # Report any errors through an exception
        if not result.passed:
            raise CommandFailure(
                f"Error(s) detected gathering {self._systemctl.service.value} "
                f"log data on {result.failed_hosts}")

        # Return the successful command output per set of hosts
        return log_data

    def display_log_data(self, log_data):
        """Display the journalctl log data.

        Args:
            log_data (dict): dictionary of journalctl log output.
        """
        self.log.info("Journalctl output:")
        for line in self.str_log_data(log_data).split("\n"):
            self.log.info(line)

    @staticmethod
    def str_log_data(log_data):
        """Get the journalctl log data as a string.

        Args:
            log_data (dict): dictionary of journalctl log output.

        Returns:
            str: the journalctl log data

        """
        data = []
        for entry in log_data:
            data.append("  {}:".format(entry["hosts"]))
            for line in entry["data"]:
                data.append("    {}".format(line))
        return "\n".join(data)

    def search_logs(self, pattern, since, until, quantity=1, timeout=60, verbose=False):
        """Search the command logs on each host for a specified string.

        Args:
            pattern (str): regular expression to search for in the logs
            since (str): search log entries from this date.
            until (str, optional): search log entries up to this date. Defaults
                to None, in which case it is not utilized.
            quantity (int, optional): number of times to expect the search
                pattern per host. Defaults to 1.
            timeout (int, optional): maximum number of seconds to wait to detect
                the specified pattern. Defaults to 60.
            verbose (bool, optional): whether or not to display the log data upon successful pattern
                detection. Defaults to False.

        Returns:
            tuple:
                (bool) - if the pattern was found quantity number of times
                (str)  - string indicating the number of patterns found in what duration

        """
        command = get_journalctl_command(since, until, units=self._systemctl.service.value)
        self.log.info("Searching for '%s' in '%s' output on %s", pattern, command, self._hosts)

        log_data = None
        detected = 0
        complete = False
        timed_out = False
        start = time.time()
        duration = 0

        # Search for patterns in the subprocess output until:
        #   - the expected number of pattern matches are detected (success)
        #   - the time out is reached (failure)
        #   - the service is no longer running (failure)
        while not complete and not timed_out and self.service_running():
            detected = 0
            log_data = self.get_log_data(self._hosts, command, timeout)
            for entry in log_data:
                match = re.findall(pattern, "\n".join(entry["data"]))
                detected += len(match) if match else 0

            complete = detected == quantity
            duration = time.time() - start
            if timeout is not None:
                timed_out = duration > timeout

            if verbose:
                self.display_log_data(log_data)

        # Summarize results
        msg = "{}/{} '{}' messages detected in".format(detected, quantity, pattern)
        runtime = "{}/{} seconds".format(duration, timeout)

        if not complete:
            # Report the error / timeout
            reason = "ERROR detected"
            details = ""
            if timed_out:
                reason = "TIMEOUT detected, exceeded {} seconds".format(timeout)
                runtime = "{} seconds".format(duration)
            if log_data:
                details = ":\n{}".format(self.str_log_data(log_data))
            self.log.info("%s - %s %s%s", reason, msg, runtime, details)
            self.log_additional_debug_data(self._hosts, since, until)

        return complete, " ".join([msg, runtime])

    def check_logs(self, pattern, since, until, quantity=1, timeout=60):
        """Check the command logs on each host for a specified string.

        Args:
            pattern (str): regular expression to search for in the logs
            since (str): search log entries from this date.
            until (str, optional): search log entries up to this date. Defaults
                to None, in which case it is not utilized.
            quantity (int, optional): number of times to expect the search
                pattern per host. Defaults to 1.
            timeout (int, optional): maximum number of seconds to wait to detect
                the specified pattern. Defaults to 60.

        Returns:
            bool: whether or not the search string was found in the logs on each
                host

        """
        # Find the pattern in the logs
        complete, message = self.search_logs(pattern, since, until, quantity, timeout, False)
        if complete:
            # Report the successful start
            self.log.info("%s subprocess startup detected - %s", self._command, message)
        return complete

    def dump_logs(self, hosts=None, timestamp=None):
        """Display the journalctl log data since detecting server start.

        Args:
            hosts (NodeSet, optional): hosts from which to display the
                journalctl log data. Defaults to None which will log the
                journalctl log data from all of the hosts.
        """
        if timestamp is None and self.timestamps["running"]:
            timestamp = self.timestamps["running"]
        elif timestamp is None and self.timestamps["verified"]:
            timestamp = self.timestamps["verified"]
        if timestamp:
            if hosts is None:
                hosts = self._hosts
            command = get_journalctl_command(timestamp, units=self._systemctl.service.value)
            self.display_log_data(self.get_log_data(hosts, command))

    def log_additional_debug_data(self, hosts, since, until):
        """Log additional information from a different journalctl command.

        Args:
            hosts (NodeSet): hosts from which to display the journalctl log data.
            since (str): search log entries from this date.
            until (str, optional): search log entries up to this date. Defaults
                to None, in which case it is not utilized.
        """
        command = get_journalctl_command(
            since, until, True, identifiers=["kernel", self._systemctl.service.value])
        details = self.str_log_data(self.get_log_data(hosts, command))
        self.log.info("Additional '%s' output:\n%s", command, details)


class Clush(JobManager):
    # pylint: disable=too-many-public-methods
    """A class for the clush job manager command."""

    def __init__(self, job):
        """Create a Clush object.

        Args:
            job (ExecutableCommand): command object to manage.
        """
        super().__init__("/run/clush/*", "clush", job)
        self.verbose = True
        self.timeout = 60

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        commands = [super().__str__(), "-w {}".format(self.hosts), str(self.job)]
        return " ".join(commands)

    def assign_environment(self, env_vars, append=False):
        """Assign or add environment variables to the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to use
                assign or add to the command
            append (bool): whether to assign (False) or append (True) the
                specified environment variables
        """
        if append:
            self.env.update(env_vars)
        else:
            self.env = EnvironmentVariables(env_vars)

    def run(self, raise_exception=None):
        """Run the command.

        Args:
            raise_exception (bool, optional): whether or not to raise an exception if the command
                fails. This overrides the self.exit_status_exception
                setting if defined. Defaults to None.

        Raises:
            CommandFailure: if there is an error running the command

        """
        if raise_exception is None:
            raise_exception = self.exit_status_exception

        if callable(self.register_cleanup_method):
            # Stop any running processes started by this job manager when the test completes
            # pylint: disable=not-callable
            self.register_cleanup_method(stop_job_manager, job_manager=self)

        command = " ".join([self.env.to_export_str(), str(self.job)]).strip()
        self.result = run_remote(self.log, self._hosts, command, self.verbose, self.timeout)

        if raise_exception and not self.result.passed:
            raise CommandFailure("Error running '{}' on {}".format(str(self.job), self.hosts))

        if self.exit_status_exception and not self.check_results():
            # Command failed if its output contains bad keywords
            if raise_exception:
                raise CommandFailure(
                    "Bad words detected in '{}' output on {}".format(str(self.job), self.hosts))

        return self.result

    def check_results(self):
        """Check the command result for any bad keywords.

        Returns:
            bool: True if either there were no items from self.check_result_list to verify or if
                none of the items were found in the command output; False if a item was found in
                the command output.

        """
        status = True
        if self.result and self.check_results_list:
            regex = r"({})".format("|".join(self.check_results_list))
            self.log.debug("Checking the command output for any bad keywords: %s", regex)
            for data in self.result.output:
                match = re.findall(regex, "\n".join(data.stdout))
                if match:
                    self.log.info(
                        "The following error messages have been detected in the '%s' output on %s:",
                        str(self.job), str(data.hosts))
                    for item in match:
                        self.log.info("  %s", item)
                    status = False
                    break
        return status
