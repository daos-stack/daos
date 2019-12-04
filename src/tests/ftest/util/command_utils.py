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
from logging import getLogger
import time
import os
import re
import signal
import yaml

from avocado.utils import process
from general_utils import pcmd, check_file_exists
from write_host_file import write_host_file


class CommandFailure(Exception):
    """Base exception for this module."""


class BasicParameter(object):
    """A class for parameters whose values are read from a yaml file."""

    def __init__(self, value, default=None):
        """Create a BasicParameter object.

        Args:
            value (object): intial value for the parameter
            default (object, optional): default value. Defaults to None.
        """
        self.value = value if value is not None else default
        self._default = default
        self.log = getLogger(__name__)

    def __str__(self):
        """Convert this BasicParameter into a string.

        Returns:
            str: the string version of the parameter's value

        """
        return str(self.value) if self.value is not None else ""

    def get_yaml_value(self, name, test, path):
        """Get the value for the parameter from the test case's yaml file.

        Args:
            name (str): name of the value in the yaml file
            test (Test): avocado Test object to use to read the yaml file
            path (str): yaml path where the name is to be found
        """
        if hasattr(test, "config") and test.config is not None:
            self.value = test.config.get(name, path, self._default)
        else:
            self.value = test.params.get(name, path, self._default)

    def update(self, value, name=None):
        """Update the value of the parameter.

        Args:
            value (object): value to assign
            name (str, optional): name of the parameter which, if provided, is
                used to display the update. Defaults to None.
        """
        self.value = value
        if name is not None:
            self.log.debug("Updated param %s => %s", name, self.value)


class FormattedParameter(BasicParameter):
    # pylint: disable=too-few-public-methods
    """A class for test parameters whose values are read from a yaml file."""

    def __init__(self, str_format, default=None):
        """Create a FormattedParameter  object.

        Args:
            str_format (str): format string used to convert the value into an
                command line argument string
            default (object): default value for the param
        """
        super(FormattedParameter, self).__init__(default, default)
        self._str_format = str_format

    def __str__(self):
        """Return a FormattedParameter object as a string.

        Returns:
            str: if defined, the parameter, otherwise an empty string

        """
        if isinstance(self._default, bool) and self.value:
            return self._str_format
        elif not isinstance(self._default, bool) and self.value is not None:
            if isinstance(self.value, dict):
                return " ".join([self._str_format.format("{} \"{}\"".format(
                    key, self.value[key])) for key in self.value])
            elif isinstance(self.value, (list, tuple)):
                return " ".join(
                    [self._str_format.format(value) for value in self.value])
            else:
                return self._str_format.format(self.value)
        else:
            return ""


class ObjectWithParameters(object):
    """A class for an object with parameters."""

    def __init__(self, namespace):
        """Create a ObjectWithParameters object.

        Args:
            namespace (str): yaml namespace (path to parameters)
        """
        self.namespace = namespace
        self.log = getLogger(__name__)

    def get_attribute_names(self, attr_type=None):
        """Get a sorted list of the names of the attr_type attributes.

        Args:
            attr_type(object, optional): A single object type or tuple of
                object types used to filter class attributes by their type.
                Defaults to None.

        Returns:
            list: a list of class attribute names used to define parameters

        """
        return [
            name for name in sorted(self.__dict__.keys())
            if attr_type is None or isinstance(getattr(self, name), attr_type)]

    def get_param_names(self):
        """Get a sorted list of the names of the BasicParameter attributes.

        Note: Override this method to change the order or inclusion of a
            command parameter in the get_params() method.

        Returns:
            list: a list of class attribute names used to define parameters

        """
        return self.get_attribute_names(BasicParameter)

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Sets each BasicParameter object's value to the yaml key that matches
        the assigned name of the BasicParameter object in this class. For
        example, the self.block_size.value will be set to the value in the yaml
        file with the key 'block_size'.

        If no key matches are found in the yaml file the BasicParameter object
        will be set to its default value.

        Args:
            test (Test): avocado Test object
        """
        for name in self.get_param_names():
            getattr(self, name).get_yaml_value(name, test, self.namespace)


class CommandWithParameters(ObjectWithParameters):
    """A class for command with paramaters."""

    def __init__(self, namespace, command, path=""):
        """Create a CommandWithParameters object.

        Uses Avocado's utils.process module to run a command str provided.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            path (str, optional): path to location of command binary file.
                Defaults to "".
        """
        super(CommandWithParameters, self).__init__(namespace)
        self._command = command
        self._path = path
        self._pre_command = None

    @property
    def command(self):
        """Get the command without its parameters."""
        return self._command

    @property
    def description(self):
        """Get the command description."""
        return self._command

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        # Join all the parameters that have been assigned a value with the
        # command to create the command string
        params = []
        for name in self.get_str_param_names():
            value = str(getattr(self, name))
            if value != "":
                params.append(value)

        # Append the path to the command and preceed it with any other
        # specified commands
        command_list = [] if self._pre_command is None else [self._pre_command]
        command_list.append(os.path.join(self._path, self._command))

        # Return the command and its parameters
        return " ".join(command_list + params)

    def get_str_param_names(self):
        """Get a sorted list of the names of the command attributes.

        Returns:
            list: a list of class attribute names used to define parameters
                for the command.

        """
        return self.get_param_names()


class ExecutableCommand(CommandWithParameters):
    """A class for command with paramaters."""

    def __init__(self, namespace, command, path="", subprocess=False):
        """Create a ExecutableCommand object.

        Uses Avocado's utils.process module to run a command str provided.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            path (str, optional): path to location of command binary file.
                Defaults to "".
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        super(ExecutableCommand, self).__init__(namespace, command, path)
        self._process = None
        self.run_as_subprocess = subprocess
        self.timeout = None
        self.verbose = True
        self.env = None
        self.sudo = False

    @property
    def process(self):
        """Getter for process attribute of the ExecutableCommand class."""
        return self._process

    def run(self):
        """Run the command.

        Raises:
            CommandFailure: if there is an error running the command

        """
        if self.run_as_subprocess:
            self._run_subprocess()
            return None
        return self._run_process()

    def _run_process(self):
        """Run the command as a foreground process.

        Raises:
            CommandFailure: if there is an error running the command

        """
        command = self.__str__()
        kwargs = {
            "cmd": command,
            "timeout": self.timeout,
            "verbose": self.verbose,
            "allow_output_check": "combined",
            "shell": True,
            "env": self.env,
            "sudo": self.sudo,
        }
        try:
            # Block until the command is complete or times out
            return process.run(**kwargs)

        except process.CmdError as error:
            # Command failed or possibly timed out
            msg = "Error occurred running '{}': {}".format(command, error)
            self.log.error(msg)
            raise CommandFailure(msg)

    def _run_subprocess(self):
        """Run the command as a sub process.

        Raises:
            CommandFailure: if there is an error running the command

        """
        if self._process is None:
            # Start the job manager command as a subprocess
            kwargs = {
                "cmd": self.__str__(),
                "verbose": self.verbose,
                "allow_output_check": "combined",
                "shell": True,
                "env": self.env,
                "sudo": self.sudo,
            }
            self._process = process.SubProcess(**kwargs)
            self._process.start()

            # Deterime if the command has launched correctly using its
            # check_subprocess_status() method.
            if not self.check_subprocess_status(self._process):
                msg = "Command '{}' did not launch correctly".format(self)
                self.log.error(msg)
                raise CommandFailure(msg)
        else:
            self.log.info("Process is already running")

    def check_subprocess_status(self, sub_process):
        """Verify command status when called in a subprocess.

        Optional method to provide a means for detecting successful command
        execution when running the command as a subprocess.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command

        Returns:
            bool: whether or not the command progress has been detected

        """
        self.log.info(
            "Checking status of the %s command in %s",
            self._command, sub_process)
        return True

    def stop(self):
        """Stop the subprocess command.

        Raises:
            CommandFailure: if unable to stop

        """
        if self._process is not None:
            # Use a list to send signals to the process with one second delays:
            #   Interupt + wait 3 seconds
            #   Terminate + wait 2 seconds
            #   Quit + wait 1 second
            #   Kill
            signal_list = [
                signal.SIGINT, None, None, None,
                signal.SIGTERM, None, None,
                signal.SIGQUIT, None,
                signal.SIGKILL]

            # Turn off verbosity to keep the logs clean as the server stops
            self._process.verbose = False

            # Keep sending signals and or waiting while the process is alive
            while self._process.poll() is None and signal_list:
                signal_type = signal_list.pop(0)
                if signal_type is not None:
                    self._process.send_signal(signal_type)
                if signal_list:
                    time.sleep(1)
            if not signal_list:
                # Indicate an error if the process required a SIGKILL
                raise CommandFailure("Error stopping '{}'".format(self))
            self._process = None


class DaosCommand(ExecutableCommand):
    """A class for similar daos command line tools."""

    def __init__(self, namespace, command, path="", timeout=60):
        """Create DaosCommand object.

        Specific type of command object built so command str returns:
            <command> <options> <request> <action/subcommand> <options>

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            path (str, optional): path to location of daos command binary.
                Defaults to ""
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 60 seconds.
        """
        super(DaosCommand, self).__init__(namespace, command, path)
        self.request = BasicParameter(None)
        self.action = BasicParameter(None)
        self.action_command = None

        # Attributes used to determine command success when run as a subprocess
        # See self.check_subprocess_status() for details.
        self.timeout = timeout
        self.pattern = None
        self.pattern_count = 1

    def get_action_command(self):
        """Assign a command object for the specified request and action."""
        self.action_command = None

    def get_param_names(self):
        """Get a sorted list of DaosCommand parameter names."""
        names = self.get_attribute_names(FormattedParameter)
        for attribute_name in ("request", "action"):
            if isinstance(getattr(self, attribute_name), BasicParameter):
                names.append(attribute_name)
        return names

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Args:
            test (Test): avocado Test object
        """
        super(DaosCommand, self).get_params(test)
        self.get_action_command()
        if isinstance(self.action_command, ObjectWithParameters):
            self.action_command.get_params(test)

    def get_str_param_names(self):
        """Get a sorted list of the names of the command attributes.

        Returns:
            list: a list of class attribute names used to define parameters
                for the command.

        """
        names = self.get_param_names()
        if self.action_command is not None:
            names[-1] = "action_command"
        return names

    def check_subprocess_status(self, sub_process):
        """Verify the status of the doas command started as a subprocess.

        Continually search the subprocess output for a pattern (self.pattern)
        until the expected number of patterns (self.pattern_count) have been
        found (typically one per host) or the timeout (self.timeout) is reached
        or the process has stopped.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command

        Returns:
            bool: whether or not the command progress has been detected

        """
        status = True
        self.log.info(
            "Checking status of the %s command in %s with a %s second timeout",
            self._command, sub_process, self.timeout)

        if self.pattern is not None:
            pattern_matches = 0
            timed_out = False
            status = False
            start_time = time.time()

            # Search for patterns in the subprocess output until:
            #   - the expected number of pattern matches are detected (success)
            #   - the time out is reached (failure)
            #   - the subprocess is no longer running (failure)
            while not status and not timed_out and sub_process.poll() is None:
                output = sub_process.get_stdout()
                pattern_matches = len(re.findall(self.pattern, output))
                status = pattern_matches == self.pattern_count
                timed_out = time.time() - start_time > self.timeout

            if not status:
                # Report the error / timeout
                err_msg = "{} detected. Only {}/{} messages received".format(
                    "Time out" if timed_out else "Error",
                    pattern_matches, self.pattern_count)
                self.log.info("%s:\n%s", err_msg, sub_process.get_stdout())

                # Stop the timed out process
                if timed_out:
                    self.stop()
            else:
                # Report the successful start
                self.log.info(
                    "%s subprocess started in %f seconds",
                    self._command, time.time() - start_time)

        return status


class EnvironmentVariables(dict):
    """Dictionary of environment variable keys and values."""

    def get_list(self):
        """Get a list of environment variable assignments.

        Returns:
            list: a list of environment variable assignment (key=value) strings

        """
        return ["{}={}".format(key, value) for key, value in self.items()]

    def get_export_str(self, separator=";"):
        """Get the command to export all of the environment variables.

        Args:
            separator (str, optional): export command separtor.
                Defaults to ";".

        Returns:
            str: a string of export commands for each environment variable

        """
        join_str = "{} export ".format(separator)
        return "export {}{}".format(join_str.join(self.get_list()), separator)


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

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        # Join the job manager command with the command to manage
        commands = [super(JobManager, self).__str__(), str(self.job)]
        if self.job.sudo:
            commands.insert(-1, "sudo -n")
        return " ".join(commands)

    def check_subprocess_status(self, subprocess):
        """Verify command status when called in a subprocess.

        Args:
            subprocess (process.SubProcess): subprocess used to run the command

        Returns:
            bool: whether or not the command progress has been detected

        """
        return self.job.check_subprocess_status(subprocess)

    def setup_command(self, env, hostfile, processes):
        """Set up the job manager command with common inputs.

        Args:
            env (EnvironmentVariables): the environment variables to use with
                the launch command
            hostfile (str): file defining host names and slots
            processes (int): number of host processes
        """
        pass


class Orterun(JobManager):
    """A class for the orterun job manager command."""

    def __init__(self, job, path="", subprocess=False):
        """Create a Orterun object.

        Args:
            job (ExecutableCommand): command object to manage.
            path (str, optional): path to location of command binary file.
                Defaults to "".
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        super(Orterun, self).__init__(
            "/run/orterun", "orterun", job, path, subprocess)

        self.hostfile = FormattedParameter("--hostfile {}", None)
        self.processes = FormattedParameter("--np {}", 1)
        self.display_map = FormattedParameter("--display-map", False)
        self.map_by = FormattedParameter("--map-by {}", "node")
        self.export = FormattedParameter("-x {}", None)
        self.enable_recovery = FormattedParameter("--enable-recovery", True)
        self.report_uri = FormattedParameter("--report-uri {}", None)
        self.allow_run_as_root = FormattedParameter(
            "--allow-run-as-root", False)
        self.mca = FormattedParameter("--mca {}", None)
        self.tag_output = FormattedParameter("--tag-output", True)
        self.ompi_server = FormattedParameter("--ompi-server {}", None)

    def add_environment_list(self, env_list):
        """Add a list of environment variables to the launch command.

        Args:
            env_vars (list): a list of environment variable names or assignments
                to use with the launch command
        """
        if self.export.value is None:
            self.export.value = []
        self.export.value.extend(env_list)

    def add_environment_variables(self, env_vars):
        """Add EnvironmentVariables to the launch command.

        Args:
            env_vars (EnvironmentVariables): the environment variables to use
                with the launch command
        """
        self.add_environment_list(env_vars.get_list())

    def setup_command(self, env, hostfile, processes):
        """Set up the orterun command with common inputs.

        Args:
            env (EnvironmentVariables): the environment variables to use with
                the launch command
            hostfile (str): file defining host names and slots
            processes (int): number of host processes
        """
        # Setup the env for the job to export with the orterun command
        self.add_environment_variables(env)

        # Setup the orterun command
        self.hostfile.value = hostfile
        self.processes.value = processes


class Mpirun(JobManager):
    """A class for the mpirun job manager command."""

    def __init__(self, job, path="", subprocess=False):
        """Create a Mpirun object.

        Args:
            job (ExecutableCommand): command object to manage.
            path (str, optional): path to location of command binary file.
                Defaults to "".
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        super(Mpirun, self).__init__(
            "/run/mpirun", "mpirun", job, path, subprocess)

        self.hostfile = FormattedParameter("-hostfile {}", None)
        self.processes = FormattedParameter("-np {}", 1)

    def setup_command(self, env, hostfile, processes):
        """Set up the mpirun command with common inputs.

        Args:
            env (EnvironmentVariables): the environment variables to use with
                the launch command
            hostfile (str): file defining host names and slots
            processes (int): number of host processes
        """
        # Setup the env for the job to export with the mpirun command
        self._pre_command = env.get_export_str()

        # Setup the orterun command
        self.hostfile.value = hostfile
        self.processes.value = processes


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

        self.label = FormattedParameter("--label", False)
        self.mpi = FormattedParameter("--mpi={}", None)
        self.export = FormattedParameter("--export={}", None)
        self.ntasks = FormattedParameter("--ntasks={}", None)
        self.distribution = FormattedParameter("--distribution={}", None)
        self.nodefile = FormattedParameter("--nodefile={}", None)

    def setup_command(self, env, hostfile, processes):
        """Set up the srun command with common inputs.

        Args:
            env (EnvironmentVariables): the environment variables to use with
                the launch command
            hostfile (str): file defining host names and slots
            processes (int): number of host processes
        """
        # Setup the env for the job to export with the srun command
        self.export.value = ",".join(["ALL"] + env.get_list())

        # Setup the srun command
        self.label.value = True
        self.mpi.value = "pmi2"
        if processes is not None:
            self.ntasks.value = processes
            self.distribution.value = "cyclic"
        if hostfile is not None:
            self.nodefile.value = hostfile


class YamlParameters(ObjectWithParameters):
    """A class of parameters used to create a yaml file."""

    def __init__(self, namespace, filename=None, title=None, other_params=None):
        """Create a YamlParameters object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            filename (str): the yaml file to generate with the parameters
            title (str, optional): namespace under which to place the
                parameters when creating the yaml file. Defaults to None.
            other_params (YamlParameters, optional): yaml parameters to
                include with these yaml parameters. Defaults to None.
        """
        super(YamlParameters, self).__init__(namespace)
        self.filename = filename
        self.title = title
        self.other_params = other_params

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        if isinstance(self.other_params, YamlParameters):
            yaml_data = self.other_params.get_yaml_data()
        else:
            yaml_data = {}
        for name in self.get_param_names():
            value = getattr(self, name).value
            if value is not None and value is not False:
                yaml_data[name] = getattr(self, name).value

        return yaml_data if self.title is None else {self.title: yaml_data}

    def create_yaml(self):
        """Create a yaml file from the parameter values.

        Raises:
            CommandFailure: if there is an error creating the yaml file

        """
        yaml_data = self.get_yaml_data()
        self.log.info("Writing yaml configuration file %s", self.filename)
        try:
            with open(self.filename, 'w') as write_file:
                yaml.dump(yaml_data, write_file, default_flow_style=False)
        except Exception as error:
            raise CommandFailure(
                "Error writing the yaml file {}: {}".format(
                    self.filename, error))

    def get_value(self, name):
        """Get the value of the specified attribute name.

        Args:
            name (str): name of the attribute from which to get the value

        Returns:
            object: the object's value referenced by the attribute name

        """
        setting = getattr(self, name, None)
        if setting is not None and isinstance(setting, BasicParameter):
            value = setting.value
        elif setting is not None:
            value = setting
        elif self.other_params is not None:
            value = self.other_params.get_value(name)
        else:
            value = None
        return value


class DaosYamlCommand(DaosCommand):
    """Defines a daos command that makes use of a yaml configuration file."""

    def __init__(self, namespace, command, yaml_config, path="", timeout=60):
        """Create a daos_server command object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            yaml_config (YamlParameters): yaml configuration parameters
            path (str, optional): path to location of daos command binary.
                Defaults to ""
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 60 seconds.
        """
        super(DaosYamlCommand, self).__init__(namespace, command, path)

        # Command configuration yaml file
        self.yaml = yaml_config

        # Command line parameters:
        # -d, --debug        Enable debug output
        # -j, --json         Enable JSON output
        # -o, --config-path= Path to agent configuration file
        self.debug = FormattedParameter("-d", False)
        self.json = FormattedParameter("-j", False)
        self.config = FormattedParameter("-o {}", self.yaml.filename)

    def get_params(self, test):
        """Get values for the daos command and its yaml config file.

        Args:
            test (Test): avocado Test object
        """
        super(DaosYamlCommand, self).get_params(test)
        self.yaml.get_params(test)
        self.yaml.create_yaml()

    # def update_log_file(self, name, index=0):
    #     """Update the logfile parameter for the daos server.

    #     Args:
    #         name (str): new log file name and path
    #         index (int, optional): server index to update. Defaults to 0.
    #     """
    #     self.yaml.update_log_file(name, index)

    def get_config_value(self, name):
        """Get the value of the yaml configuration parameter name.

        Args:
            name (str): name of the yaml configuration parameter from which to
                get the value

        Returns:
            object: the yaml configuration parameter value or None

        """
        return self.yaml.get_value(name)


class SubprocessManager(Orterun):
    """Defines an object that manages a sub process launched with orterun."""

    def __init__(self, namespace, command, path=""):
        """Create a SubprocessManager object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (DaosCommand): command to manage
            path (str): Path to orterun binary
        """
        super(SubprocessManager, self).__init__(command, path, True)
        self.namespace = namespace

        # Define the list of hosts that will execute the daos command
        self._hosts = []

        # Define the list of executable names to terminate in the kill() method
        self._exe_names = [self.job.command]

    @property
    def hosts(self):
        """Get the hosts used to execute the daos command."""
        return self._hosts

    @hosts.setter
    def hosts(self, value):
        """Set the hosts used to execute the daos command.

        Args:
            value (tuple): a tuple of a list of hosts, a path in which to create
                the hostfile, and a number of slots to specify per host in the
                hostfile (can be None)
        """
        self._hosts, path, slots = value
        self.processes.value = len(self._hosts)
        self.hostfile.value = self.create_hostfile(path, slots)
        if hasattr(self.job, "pattern_count"):
            self.job.pattern_count = len(self._hosts)

    def create_hostfile(self, path, slots):
        """Create a new hostfile for the hosts."""
        return write_host_file(self._hosts, path, slots)

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Use the yaml file paramter values to assign the server command and
        orterun command parameters.

        Args:
            test (Test): avocado Test object
        """
        # Get the parameters for the Orterun command
        super(SubprocessManager, self).get_params(test)

        # Get the values for the job parameters
        self.job.get_params(test)

    def start(self):
        """Start the daos command with orterun.

        Raises:
            CommandFailure: if the daos command fails to start

        """
        try:
            self.run()
        except CommandFailure:
            # Kill the subprocess, anything that might have started
            self.kill()
            raise CommandFailure(
                "Failed to start {}.".format(self.job.description))

    def kill(self):
        """Forcably terminate any sub process running on hosts."""
        kill_cmds = [
            "sudo pkill '({})' --signal INT".format("|".join(self._exe_names)),
            "if pgrep -l '({})'".format("|".join(self._exe_names)),
            "then sleep 5",
            "pkill '({})' --signal KILL".format("|".join(self._exe_names)),
            "pgrep -l '({})'".format("|".join(self._exe_names)),
            "fi"
        ]
        self.log.info("Killing any %s processes", "|".join(self._exe_names))
        pcmd(self._hosts, "; ".join(kill_cmds), False, None, None)

    def verify_socket_directory(self, user):
        """Verify the domain socket directory is present and owned by this user.

        Args:
            user (str): user to verify has ownership of the directory

        Raises:
            CommandFailure: if the socket directory does not exist or is not
                owned by the user

        """
        if self._hosts and hasattr(self._command, "yaml"):
            directory = self._command.yaml.socket_dir.value
            status, nodes = check_file_exists(self._hosts, directory, user)
            if not status:
                raise CommandFailure(
                    "{}: Server missing socket directory {} for user {}".format(
                        nodes, directory, user))

    def get_config_value(self, name):
        """Get the value of the yaml configuration parameter name.

        Args:
            name (str): name of the yaml configuration parameter from which to
                get the value

        Returns:
            object: the yaml configuration parameter value or None

        """
        value = None
        if self.job is not None and hasattr(self.job, "get_config_value"):
            value = self.job.get_config_value(name)
        return value
