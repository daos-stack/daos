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
# pylint: disable=too-many-lines
from logging import getLogger
import time
import os
import re
import signal
import yaml

from avocado.utils import process
from general_utils import check_file_exists, stop_processes
from write_host_file import write_host_file

from env_modules import load_mpi


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


class NamedParameter(BasicParameter):
    # pylint: disable=too-few-public-methods
    """A class for test parameters whose values are read from a yaml file.

    This is essentially a BasicParameter object whose yaml value is obtained
    with a different name than the one assigned to the object.
    """

    def __init__(self, name, value, default=None):
        """Create a NamedParameter  object.

        Args:
            name (str): yaml key name
            value (object): intial value for the parameter
            default (object): default value for the param
        """
        super(NamedParameter, self).__init__(value, default)
        self._name = name

    def get_yaml_value(self, name, test, path):
        """Get the value for the parameter from the test case's yaml file.

        Args:
            name (str): name of the value in the yaml file - not used
            test (Test): avocado Test object to use to read the yaml file
            path (str): yaml path where the name is to be found
        """
        return super(NamedParameter, self).get_yaml_value(
            self._name, test, path)


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
    def path(self):
        """Get the path used for the command."""
        return self._path

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
        self.exit_status_exception = True
        self.verbose = True
        self.env = None
        self.sudo = False

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        value = super(ExecutableCommand, self).__str__()
        if self.sudo:
            value = " ".join(["sudo -n", value])
        return value

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
            "ignore_status": not self.exit_status_exception,
            "allow_output_check": "combined",
            "shell": True,
            "env": self.env,
            # "sudo": self.sudo,
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


class CommandWithSubCommand(ExecutableCommand):
    """A class for a command with a sub command."""

    def __init__(self, namespace, command, path="", subprocess=False):
        """Create a CommandWithSubCommand object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            path (str, optional): path to location of command binary file.
                Defaults to "".
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        super(CommandWithSubCommand, self).__init__(namespace, command, path)

        # Define the sub-command parameter whose value is used to assign the
        # sub-command's CommandWithParameters-based class.  Use the command to
        # create uniquely named yaml parameter names.
        self.sub_command = NamedParameter(
            "{}_sub_command".format(self._command), None)

        # The class used to define the sub-command and it's specific parameters.
        # Multiple sub-commands may be available, but only one can be active at
        # a given time.  The self.get_sub_command_class() method is called after
        # obtaining the main command's parameter values, in self.get_params(),
        # to assign the sub-command's class.  This is typically a class based
        # upon CommandWithParameters class, but can be anything with a __str__()
        # method.
        self.sub_command_class = None

    def get_param_names(self):
        """Get a sorted list of the names of the BasicParameter attributes.

        Ensure the sub command appears at the end of the list

        Returns:
            list: a list of class attribute names used to define parameters

        """
        names = self.get_attribute_names(BasicParameter)
        names.append(names.pop(names.index("sub_command")))
        return names

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Args:
            test (Test): avocado Test object
        """
        super(CommandWithSubCommand, self).get_params(test)
        self.get_sub_command_class()
        if isinstance(self.sub_command_class, ObjectWithParameters):
            self.sub_command_class.get_params(test)

    def get_sub_command_class(self):
        """Get the class assignment for the sub command.

        Override this method with sub_command_class assignment that maps to the
        expected sub_command value.
        """
        self.sub_command_class = None

    def get_str_param_names(self):
        """Get a sorted list of the names of the command attributes.

        If the sub-command parameter yields a sub-command class, replace the
        sub-command value with the resulting string from the sub-command class
        when assembling that command string.

        Returns:
            list: a list of class attribute names used to define parameters
                for the command.

        """
        names = self.get_param_names()
        if self.sub_command_class is not None:
            index = names.index("sub_command")
            names[index] = "sub_command_class"
        return names

    def set_sub_command(self, value):
        """Set the command's sub-command value and update the sub-command class.

        Args:
            value (str): sub-command value
        """
        self.sub_command.value = value
        self.get_sub_command_class()


class SubProcessCommand(CommandWithSubCommand):
    """A class for a command run as a subprocess with a sub command."""

    def __init__(self, namespace, command, path="", timeout=60):
        """Create a SubProcessCommand object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            path (str, optional): path to location of command binary file.
                Defaults to "".
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 60 seconds.
        """
        super(SubProcessCommand, self).__init__(namespace, command, path, True)

        # Attributes used to determine command success when run as a subprocess
        # See self.check_subprocess_status() for details.
        self.timeout = timeout
        self.pattern = None
        self.pattern_count = 1

    def check_subprocess_status(self, sub_process):
        """Verify the status of the command started as a subprocess.

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

    def check_subprocess_status(self, sub_process):
        """Verify command status when called in a subprocess.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command

        Returns:
            bool: whether or not the command progress has been detected

        """
        return self.job.check_subprocess_status(sub_process)

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

    def run(self):
        """Run the orterun command.

        Raises:
            CommandFailure: if there is an error running the command

        """
        load_mpi("openmpi")
        return super(Orterun, self).run()


class Mpirun(JobManager):
    """A class for the mpirun job manager command."""

    def __init__(self, job, path="", subprocess=False, mpitype="openmpi"):
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
        self.mpitype = mpitype

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

    def run(self):
        """Run the mpirun command.

        Raises:
            CommandFailure: if there is an error running the command

        """
        load_mpi(self.mpitype)
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


class YamlCommand(SubProcessCommand):
    """Defines a sub-process command that utilizes a yaml configuration file."""

    def __init__(self, namespace, command, path="", yaml_cfg=None, timeout=60):
        """Create a YamlCommand command object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            yaml_cfg (YamlParameters, optional): yaml configuration parameters.
                Defaults to None.
            path (str, optional): path to location of daos command binary.
                Defaults to ""
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 60 seconds.
        """
        super(YamlCommand, self).__init__(namespace, command, path)

        # Command configuration yaml file
        self.yaml = yaml_cfg

    def get_params(self, test):
        """Get values for the daos command and its yaml config file.

        Args:
            test (Test): avocado Test object
        """
        super(YamlCommand, self).get_params(test)
        if isinstance(self.yaml, YamlParameters):
            self.yaml.get_params(test)
            self.yaml.create_yaml()

    def get_config_value(self, name):
        """Get the value of the yaml configuration parameter name.

        Args:
            name (str): name of the yaml configuration parameter from which to
                get the value

        Returns:
            object: the yaml configuration parameter value or None

        """
        value = None
        if isinstance(self.yaml, YamlParameters):
            value = self.yaml.get_value(name)
        return value

    @property
    def access_points(self):
        """Get the access points used with this command.

        Returns:
            AccessPoints: an object with the list of hosts and ports that serve
                as access points for the command.

        """
        value = None
        if isinstance(self.yaml, YamlParameters):
            value = self.yaml.get_config_value("access_points")
        return value


class SubprocessManager(Orterun):
    """Defines an object that manages a sub process launched with orterun."""

    def __init__(self, namespace, command, path=""):
        """Create a SubprocessManager object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (YamlCommand): command to manage
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
                "Failed to start {}.".format(str(self.job)))

    def kill(self):
        """Forcably terminate any sub process running on hosts."""
        stop_processes(self._hosts, "'({})'".format("|".join(self._exe_names)))

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


class TransportCredentials(YamlParameters):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self, namespace, title):
        """Initialize a TransportConfig object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            title (str, optional): namespace under which to place the
                parameters when creating the yaml file. Defaults to None.
        """
        super(TransportCredentials, self).__init__(namespace, None, title)

        # Transport credential parameters:
        #   - allow_insecure: false|true
        #       Specify 'false' to bypass loading certificates and use insecure
        #       communications channnels
        #
        #   - ca_cert: <file>, e.g. ".daos/daosCA.crt"
        #       Custom CA Root certificate for generated certs
        #
        #   - cert: <file>, e.g. ".daos/daos_agent.crt"
        #       Agent certificate for use in TLS handshakes
        #
        #   - key: <file>, e.g. ".daos/daos_agent.key"
        #       Key portion of Server Certificate
        #
        self.allow_insecure = BasicParameter(True, True)
        self.ca_cert = BasicParameter(None)
        self.cert = BasicParameter(None)
        self.key = BasicParameter(None)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        yaml_data = super(TransportCredentials, self).get_yaml_data()

        # Convert the boolean value into a string
        if self.title is not None:
            yaml_data[self.title]["allow_insecure"] = self.allow_insecure.value
        else:
            yaml_data["allow_insecure"] = self.allow_insecure.value

        return yaml_data


class CommonConfig(YamlParameters):
    """Defines common daos_agent and daos_server configuration file parameters.

    Includes:
        - the daos system name (name)
        - a list of access point nodes (access_points)
        - the default port number (port)
    """

    def __init__(self, name, transport):
        """Initialize a CommonConfig object.

        Args:
            name (str): default value for the name configuration parameter
            transport (TransportCredentials): transport credentails
        """
        super(CommonConfig, self).__init__(
            "/run/common_config/*", None, None, transport)

        # Common configuration paramters
        #   - name: <str>, e.g. "daos_server"
        #       Name associated with the DAOS system.
        #
        #   - access_points: <list>, e.g.  ["hostname1:10001"]
        #       Hosts can be specified with or without port, default port below
        #       assumed if not specified. Defaults to the hostname of this node
        #       at port 10000 for local testing
        #
        #   - port: <int>, e.g. 10001
        #       Default port number with whith to bind the daos_server. This
        #       will also be used when connecting to access points if the list
        #       only contains host names.
        #
        self.name = BasicParameter(None, name)
        self.access_points = AccessPoints(10001)
        self.port = BasicParameter(None, 10001)

    def update_hosts(self, hosts):
        """Update the list of hosts for the access point.

        Args:
            hosts (list): list of access point hosts
        """
        if isinstance(hosts, list):
            self.access_points.hosts = [host for host in hosts]
        else:
            self.access_points.hosts = []

    def get_params(self, test):
        """Get values for starting agents and server from the yaml file.

        Obtain the lists of hosts from the BasicParameter class attributes.

        Args:
            test (Test): avocado Test object
        """
        # Get the common parameters: name & port
        super(CommonConfig, self).get_params(test)
        self.access_points.port = self.port.value

        # Get the transport credentials parameters
        self.other_params.get_params(test)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        yaml_data = super(CommonConfig, self).get_yaml_data()
        yaml_data.pop("pmix", None)

        # For now only include the first host in the access point list
        if self.access_points.hosts:
            yaml_data["access_points"] = [
                ":".join([host, str(self.port.value)])
                for host in self.access_points.hosts[:1]
            ]

        return yaml_data


class AccessPoints(object):
    # pylint: disable=too-few-public-methods
    """Defines an object for storing access point data."""

    def __init__(self, port=10001):
        """Initialize a AccessPoints object.

        Args:
            port (int, optional): port number. Defaults to 10001.
        """
        self.hosts = []
        self.port = port

    def __str__(self):
        """Return a comma-separated list of <host>:<port>."""
        return ",".join(
            [":".join([host, str(self.port)]) for host in self.hosts])
