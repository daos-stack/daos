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
from datetime import datetime
from distutils.spawn import find_executable
import os
import re

from ClusterShell.NodeSet import NodeSet

from command_utils import ExecutableCommand, SystemctlCommand
from command_utils_base import FormattedParameter, EnvironmentVariables
from command_utils_base import CommandFailure
from env_modules import load_mpi
from general_utils import pcmd, run_task
from write_host_file import write_host_file


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
        super(JobManager, self).__init__(namespace, command, path, subprocess)
        self.job = job
        self._hosts = None

    @property
    def hosts(self):
        """Get the list of hosts associated with this command."""
        return self._hosts

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        commands = [super(JobManager, self).__str__(), str(self.job)]
        return " ".join(commands)

    def check_subprocess_status(self, sub_process):
        """Verify command status when called in a subprocess.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command

        Returns:
            bool: whether or not the command progress has been detected

        """
        return self.job.check_subprocess_status(sub_process)

    def assign_hosts(self, hosts, path=None, slots=None):
        """Assign the hosts to use with the command.

        Set the appropriate command line parameter with the specified value.

        Args:
            hosts (list): list of hosts to specify on the command line
            path (str, optional): path to use when specifying the hosts through
                a hostfile. Defaults to None.
            slots (int, optional): number of slots per host to specify in the
                optional hostfile. Defaults to None.
        """
        pass

    def assign_processes(self, processes):
        """Assign the number of processes per node.

        Set the appropriate command line parameter with the specified value.

        Args:
            processes (int): number of processes per node
        """
        pass

    def assign_environment(self, env_vars, append=False):
        """Assign or add environment variables to the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to use
                assign or add to the command
            append (bool): whether to assign (False) or append (True) the
                specified environment variables
        """
        pass

    def assign_environment_default(self, env_vars):
        """Assign the default environment variables for the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to
                assign as the default
        """
        pass

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
        state = super(JobManager, self).get_subprocess_state(message)
        if self._process is not None and self._hosts:
            # Display the status of the remote job processes on each host
            command = "/usr/bin/pgrep -a {}".format(self.job.command_regex)
            self.log.debug(
                "%s processes still running remotely%s:", self.command,
                " {}".format(message) if message else "")
            self.log.debug("Running (on %s): %s", self._hosts, command)
            results = pcmd(self._hosts, command, True, 10, None)

            # Add a running state to the list of process states if any remote
            # process was found to be active.  The pcmd method will return a
            # dictionary with a single key, e.g. {1: <NodeSet>}, if there are
            # no remote processes running on any of the hosts.  If this value
            # is not returned, indicate there are processes running by adding
            # the "R" state to the process state list.
            if 1 not in results or len(results) > 1:
                if not state:
                    state = ["?"]
                state.append("R")
        return state


class Orterun(JobManager):
    """A class for the orterun job manager command."""

    def __init__(self, job, subprocess=False):
        """Create a Orterun object.

        Args:
            job (ExecutableCommand): command object to manage.
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        if not load_mpi("openmpi"):
            raise CommandFailure("Failed to load openmpi")

        path = os.path.dirname(find_executable("orterun"))
        super(Orterun, self).__init__(
            "/run/orterun/*", "orterun", job, path, subprocess)

        # Default mca values to avoid queue pair errors
        mca_default = {
            "btl_openib_warn_default_gid_prefix": "0",
            "btl": "tcp,self",
            "oob": "tcp",
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

    def assign_hosts(self, hosts, path=None, slots=None):
        """Assign the hosts to use with the command (--hostfile).

        Args:
            hosts (list): list of hosts to specify in the hostfile
            path (str, optional): hostfile path. Defaults to None.
            slots (int, optional): number of slots per host to specify in the
                hostfile. Defaults to None.
        """
        self._hosts = hosts
        kwargs = {"hostlist": self._hosts, "slots": slots}
        if path is not None:
            kwargs["path"] = path
        self.hostfile.value = write_host_file(**kwargs)

    def assign_processes(self, processes):
        """Assign the number of processes per node (-np).

        Args:
            processes (int): number of processes per node
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
            original = EnvironmentVariables({
                item.split("=")[0]: item.split("=")[1] if "=" in item else None
                for item in self.export.value})
            original.update(env_vars)
            self.export.value = original.get_list()
        else:
            # Overwrite the environmental variable assignment
            self.export.value = env_vars.get_list()

    def assign_environment_default(self, env_vars):
        """Assign the default environment variables for the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to
                assign as the default
        """
        self.export.update_default(env_vars.get_list())

    def run(self):
        """Run the orterun command.

        Raises:
            CommandFailure: if there is an error running the command

        """
        if not load_mpi("openmpi"):
            raise CommandFailure("Failed to load openmpi")

        return super(Orterun, self).run()


class Mpirun(JobManager):
    """A class for the mpirun job manager command."""

    def __init__(self, job, subprocess=False, mpitype="openmpi"):
        """Create a Mpirun object.

        Args:
            job (ExecutableCommand): command object to manage.
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        if not load_mpi(mpitype):
            raise CommandFailure("Failed to load {}".format(mpitype))

        path = os.path.dirname(find_executable("mpirun"))
        super(Mpirun, self).__init__(
            "/run/mpirun", "mpirun", job, path, subprocess)

        mca_default = None
        if mpitype == "openmpi":
            # Default mca values to avoid queue pair errors w/ OpenMPI
            mca_default = {
                "btl_openib_warn_default_gid_prefix": "0",
                "btl": "tcp,self",
                "oob": "tcp",
                "pml": "ob1",
                "btl_tcp_if_include": "eth0",
            }

        self.hostfile = FormattedParameter("-hostfile {}", None)
        self.processes = FormattedParameter("-np {}", 1)
        self.ppn = FormattedParameter("-ppn {}", None)
        self.envlist = FormattedParameter("-envlist {}", None)
        self.mca = FormattedParameter("--mca {}", mca_default)
        self.working_dir = FormattedParameter("-wdir {}", None)
        self.mpitype = mpitype

    def assign_hosts(self, hosts, path=None, slots=None):
        """Assign the hosts to use with the command (-f).

        Args:
            hosts (list): list of hosts to specify in the hostfile
            path (str, optional): hostfile path. Defaults to None.
            slots (int, optional): number of slots per host to specify in the
                hostfile. Defaults to None.
        """
        self._hosts = hosts
        kwargs = {"hostlist": self._hosts, "slots": slots}
        if path is not None:
            kwargs["path"] = path
        self.hostfile.value = write_host_file(**kwargs)

    def assign_processes(self, processes):
        """Assign the number of processes per node (-np).

        Args:
            processes (int): number of processes per node
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
        # Pass the environment variables via the process.run method env argument
        if append and self.env is not None:
            # Update the existing dictionary with the new values
            self.env.update(env_vars)
        else:
            # Overwrite/create the dictionary of environment variables
            self.env = EnvironmentVariables(env_vars)

    def assign_environment_default(self, env_vars):
        """Assign the default environment variables for the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to
                assign as the default
        """
        self.envlist.update_default(env_vars.get_list())

    def run(self):
        """Run the mpirun command.

        Raises:
            CommandFailure: if there is an error running the command

        """
        if not load_mpi(self.mpitype):
            raise CommandFailure("Failed to load {}".format(self.mpitype))

        return super(Mpirun, self).run()


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
        super(Srun, self).__init__("/run/srun", "srun", job, path, subprocess)

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

    def assign_hosts(self, hosts, path=None, slots=None):
        """Assign the hosts to use with the command (-f).

        Args:
            hosts (list): list of hosts to specify in the hostfile
            path (str, optional): hostfile path. Defaults to None.
            slots (int, optional): number of slots per host to specify in the
                hostfile. Defaults to None.
        """
        self._hosts = hosts
        kwargs = {"hostlist": self._hosts, "slots": None}
        if path is not None:
            kwargs["path"] = path
        self.nodefile.value = write_host_file(**kwargs)
        self.ntasks_per_node.value = slots

    def assign_processes(self, processes):
        """Assign the number of processes per node (--ntasks).

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
            original = EnvironmentVariables({
                item.split("=")[0]: item.split("=")[1] if "=" in item else None
                for item in self.export.value.split(",")})
            original.update(env_vars)
            self.export.value = ",".join(original.get_list())
        else:
            # Overwrite the environmental variable assignment
            self.export.value = ",".join(env_vars.get_list())

    def assign_environment_default(self, env_vars):
        """Assign the default environment variables for the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to
                assign as the default
        """
        self.export.update_default(env_vars.get_list())


class Systemctl(JobManager):
    """A class for the systemctl job manager command."""

    def __init__(self, job):
        """Create a Orterun object.

        Args:
            job (SubProcessCommand): command object to manage.
        """
        # path = os.path.dirname(find_executable("systemctl"))
        super(Systemctl, self).__init__("/run/systemctl/*", "", job)
        self.job = job
        self._systemctl = SystemctlCommand()
        self._systemctl.service.value = self.job.service_name

        self.timestamps = {
            "enable": None,
            "disable": None,
            "start": None,
            "stop": None,
            "restart": None,
        }

    @property
    def hosts(self):
        """Get the list of hosts associated with this command."""
        return list(self._hosts) if self._hosts else None

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        return self._systemctl.__str__()

    def _run_unit_command(self, command):
        """Run the systemctl command.

        Args:
            command (str): systemctl unit command

        Raises:
            CommandFailure: if there is an issue running the command

        Returns:
            dict: a dictionary of return codes keys and accompanying NodeSet
                values indicating which hosts yielded the return code.

        """
        self._systemctl.unit_command.value = command
        self.timestamps[command] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        result = pcmd(self._hosts, self.__str__(), self.verbose, self.timeout)
        if 255 in result:
            raise CommandFailure(
                "Timeout detected running '{}' with a {}s timeout on {}".format(
                    self.__str__(), self.timeout, NodeSet.fromlist(result[255]))
                )
        if 0 not in result or len(result) > 1:
            failed = []
            for item, value in result.items():
                if item != 0:
                    failed.extend(value)
            raise CommandFailure("Error occurred running '{}' on {}".format(
                self.__str__(), NodeSet.fromlist(failed)))
        return result

    def run(self):
        """Start the job's service via the systemctl command.

        Raises:
            CommandFailure: if unable to start

        """
        return self._run_unit_command("start")

    def enable(self):
        """Enable the job's service via the systemctl command.

        Raises:
            CommandFailure: if unable to enable

        """
        return self._run_unit_command("enable")

    def disable(self):
        """Disable the job's service via the systemctl command.

        Raises:
            CommandFailure: if unable to disable

        """
        return self._run_unit_command("disable")

    def stop(self):
        """Stop the job's service via the systemctl command.

        Raises:
            CommandFailure: if unable to stop

        """
        return self._run_unit_command("stop")

    def status(self):
        """Get the status of the job's service via the systemctl command.

        Raises:
            CommandFailure: if unable to get the status

        """
        return self._run_unit_command("status")

    def get_log_data(self, hosts, since, until=None, timeout=60):
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
            hosts (list): list of hosts from which to gather log data.
            since (str): show log entries from this date.
            until (str, optional): show log entries up to this date. Defaults
                to None, in which case it is not utilized.
            timeout (int, optional): timeout for issuing the command. Defaults
                to 60 seconds.

        Returns:
            dict: log output per host

        """
        self.log.info(
            "Gathering %s log data on %s from %s%s",
            self.job.service.value, str(hosts), since,
            " ".join(("", "to", until)) if until else "")

        # Setup the journalctl command to capture all unit activity from the
        # specified start date to now or a specified end date
        #   --output=json?
        command = [
            "journalctl",
            "--unit={}".format(self.job.service.value),
            "--since={}".format(since),
        ]
        if until:
            command.append("--until={}".format(until))

        # Gather the log information per host
        task = run_task(hosts, " ".join(command), timeout)

        # Create a dictionary of hosts for each unique return code
        results = {code: hosts for code, hosts in task.iter_retcodes()}

        # Determine if the command completed successfully across all the hosts
        status = len(results) == 1 and 0 in results

        # Determine if any commands timed out
        timed_out = [str(hosts) for hosts in task.iter_keys_timeout()]
        if timed_out:
            status = False
        if not status:
            self.log.info("  Errors detected running \"%s\":", command)

        # List any hosts that timed out
        if timed_out:
            self.log.info(
                "    %s: timeout detected after %s seconds",
                str(NodeSet.fromlist(timed_out)), timeout)

        # Display/return the command output
        log_data = {}
        for code in sorted(results):
            # Get the command output from the hosts with this return code
            output_data = list(task.iter_buffers(results[code]))
            if not output_data:
                output_data = [["<NONE>", results[code]]]

            for output_buffer, output_hosts in output_data:
                node_set = NodeSet.fromlist(output_hosts)
                lines = str(output_buffer).splitlines()

                if status:
                    # Add the successful output from each node to the dictionary
                    log_data[node_set] = lines
                else:
                    # Display all of the results in the case of an error
                    if len(lines) > 1:
                        self.log.info("    %s: rc=%s, output:", node_set, code)
                        for line in lines:
                            self.log.info("      %s", line)
                    else:
                        self.log.info(
                            "    %s: rc=%s, output: %s",
                            node_set, code, output_buffer)

        # Report any errors through an exception
        if not status:
            raise CommandFailure(
                "Error(s) detected gathering {} log data on {}".format(
                    self.job.service.value, NodeSet.fromlist(hosts)))

        # Return the successful command output per set of hosts
        return log_data

    def check_logs(self, search, since, until, quantity=1, timeout=60):
        """Check the command logs on each host for a specified string.

        Args:
            search (str): regular expression to search for in the logs
            since (str): search log entries from this date.
            until (str, optional): search log entries up to this date. Defaults
                to None, in which case it is not utilized.
            quantity (int, optional): number of times to expect the search
                pattern per host. Defaults to 1.
            timeout (int, optional): [description]. Defaults to 60.

        Returns:
            bool: whether or not the search string was found in the logs on each
                host

        """
        status = True
        self.log.info(
            "Searching for '%s' in '%s' output on %s",
            search, self._systemctl, self._hosts)
        log_data = self.get_log_data(self._hosts, since, until, timeout)
        for host in sorted(log_data):
            match = re.findall(search, log_data[host])
            if not match:
                self.log.info("  %s: '%s' not found in log", host, search)
                status = False
            else:
                self.log.info(
                    "  %s: %s/%s '%s' patterns found in log",
                    host, len(match), quantity, search)
                if len(match) < quantity:
                    status = False
        return status

    def check_subprocess_status(self, sub_process):
        """Verify command status when called in a subprocess.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command

        Returns:
            bool: whether or not the command progress has been detected

        """
        # return self.job.check_subprocess_status(sub_process)
        return self.check_logs(
            self.job.pattern, self.timestamps["start"], None,
            self.job.pattern_count.value, self.job.pattern_timeout.value)

    def assign_hosts(self, hosts, path=None, slots=None):
        """Assign the hosts to use with the command.

        Set the appropriate command line parameter with the specified value.

        Args:
            hosts (list): list of hosts to specify on the command line
            path (str, optional): path to use when specifying the hosts through
                a hostfile. Defaults to None. Not used.
            slots (int, optional): number of slots per host to specify in the
                optional hostfile. Defaults to None. Not used.
        """
        self._hosts = NodeSet.fromlist(hosts)

    def assign_environment(self, env_vars, append=False):
        """Assign or add environment variables to the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to use
                assign or add to the command
            append (bool): whether to assign (False) or append (True) the
                specified environment variables
        """
        pass

    def assign_environment_default(self, env_vars):
        """Assign the default environment variables for the command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to
                assign as the default
        """
        pass

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
        return []
