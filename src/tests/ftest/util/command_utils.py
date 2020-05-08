#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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

from avocado.utils import process

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
        self.log = getLogger(self.__class__.__name__)

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

    def update(self, value, name=None, append=False):
        """Update the value of the parameter.

        Args:
            value (object): value to assign
            name (str, optional): name of the parameter which, if provided, is
                used to display the update. Defaults to None.
            append (bool, optional): appemnd/extend/update the current list/dict
                with the provided value.  Defaults to False - override the
                current value.
        """
        if append and isinstance(self.value, list):
            if isinstance(value, list):
                # Add the new list of value to the existing list
                self.value.extend(value)
            else:
                # Add the new value to the existing list
                self.value.append(value)
        elif append and isinstance(self.value, dict):
            # Update the dictionary with the new key/value pairs
            self.value.update(value)
        else:
            # Override the current value with the new value
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
        parameter = ""
        if isinstance(self._default, bool) and self.value:
            parameter = self._str_format
        elif not isinstance(self._default, bool) and self.value is not None:
            if isinstance(self.value, dict):
                parameter = " ".join([
                    self._str_format.format(
                        "{} \"{}\"".format(key, self.value[key]))
                    for key in self.value])
            elif isinstance(self.value, (list, tuple)):
                parameter = " ".join(
                    [self._str_format.format(value) for value in self.value])
            else:
                parameter = self._str_format.format(self.value)
        return parameter


class ObjectWithParameters(object):
    """A class for an object with parameters."""

    def __init__(self, namespace):
        """Create a ObjectWithParameters object.

        Args:
            namespace (str): yaml namespace (path to parameters)
        """
        self.namespace = namespace
        self.log = getLogger(self.__class__.__name__)

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

    # A list of regular expressions for each class method that produces a
    # CmdResult object.  Used by the self.get_output() method to return specific
    # values from the standard ouput yielded by the method.
    METHOD_REGEX = {"run": r"(.*)"}

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
            # Do not consider a lingering orterun process to be a test error.
            if not signal_list and self._command != "orterun":
                # Indicate an error if the process required a SIGKILL
                raise CommandFailure("Error stopping '{}'".format(self))
            self._process = None

    def get_output(self, method_name, **kwargs):
        """Get output from the command issued by the specified method.

        Issue the specified method and return a list of strings that result from
        searching its standard output for a fixed set of patterns defined for
        the class method.

        Args:
            method_name (str): name of the method to execute

        Raises:
            CommandFailure: if there is an error finding the method, finding the
                method's regex pattern, or executing the method

        Returns:
            list: a list of strings obtained from the method's output parsed
                through its regex

        """
        # Get the method to call to obtain the CmdResult
        method = getattr(self, method_name)
        if method is None:
            raise CommandFailure(
                "No '{}()' method defined for this class".format(method_name))

        # Get the regex pattern to filter the CmdResult.stdout
        if method_name not in self.METHOD_REGEX:
            raise CommandFailure(
                "No pattern regex defined for '{}()'".format(method_name))
        pattern = self.METHOD_REGEX[method_name]

        # Run the command and parse the output using the regex
        result = method(**kwargs)
        if not isinstance(result, process.CmdResult):
            raise CommandFailure(
                "{}() did not return a CmdResult".format(method_name))
        return re.findall(pattern, result.stdout)


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
        #
        # This parameter can be specified in the test yaml like so:
        #   <command>:
        #       <command>_sub_command: <sub_command>
        #       <sub_command>:
        #           <sub_command>_sub_command: <sub_command_sub_command>
        #
        self.sub_command = NamedParameter(
            "{}_sub_command".format(self._command), None)

        # Define the class to represent the active sub-command and it's specific
        # parameters.  Multiple sub-commands may be available, but only one can
        # be active at a given time.
        #
        # The self.get_sub_command_class() method is called after obtaining the
        # main command's parameter values, in self.get_params(), to assign the
        # sub-command's class.  This is typically a class based upon the
        # CommandWithParameters class, but can be any object with a __str__()
        # method (including a simple str object).
        #
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

        Calls self.get_sub_command_class() to assign the self.sub_command_class
        after obtaining the latest self.sub_command definition.  If the
        self.sub_command_class is assigned to an ObjectWithParameters-based
        class its get_params() method is also called.

        Args:
            test (Test): avocado Test object
        """
        super(CommandWithSubCommand, self).get_params(test)
        self.get_sub_command_class()
        if isinstance(self.sub_command_class, ObjectWithParameters):
            self.sub_command_class.get_params(test)

    def get_sub_command_class(self):
        """Get the class assignment for the sub command.

        Should be overridden to assign the self.sub_command_class using the
        latest self.sub_command definition.

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


class DaosCommand(ExecutableCommand):
    """A class for similar daos command line tools."""

    def __init__(self, namespace, command, path=""):
        """Create DaosCommand object.

        Specific type of command object built so command str returns:
            <command> <options> <request> <action/subcommand> <options>

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            path (str): path to location of daos command binary.
        """
        super(DaosCommand, self).__init__(namespace, command, path)
        self.request = BasicParameter(None)
        self.action = BasicParameter(None)
        self.action_command = None

    def get_action_command(self):
        """Assign a command object for the specified request and action."""
        self.action_command = None

    def get_param_names(self):
        """Get a sorted list of DaosCommand parameter names."""
        names = self.get_attribute_names(FormattedParameter)
        names.extend(["request", "action"])
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

        # Default mca values to avoid queue pair errors
        mca_default = {
            "btl_openib_warn_default_gid_prefix": "0",
            "btl": "tcp,self",
            "oob": "tcp",
            "pml": "ob1",
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

    def setup_command(self, env, hostfile, processes):
        """Set up the orterun command with common inputs.

        Args:
            env (EnvironmentVariables): the environment variables to use with
                the launch command
            hostfile (str): file defining host names and slots
            processes (int): number of host processes
        """
        # Setup the env for the job to export with the orterun command
        if self.export.value is None:
            self.export.value = []
        self.export.value.extend(env.get_list())

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
        self.ppn = FormattedParameter("-ppn {}", None)
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
        self.nodelist = FormattedParameter("--nodelist={}", None)
        self.ntasks_per_node = FormattedParameter("--ntasks-per-node={}", None)
        self.reservation = FormattedParameter("--reservation={}", None)
        self.partition = FormattedParameter("--partition={}", None)
        self.output = FormattedParameter("--output={}", None)

    def setup_command(self, env, hostfile, processes):
        """Set up the srun command with common inputs.

        Args:
            env (EnvironmentVariables): the environment variables to use with
                the launch command
            hostfile (str): file defining host names and slots
            processes (int): number of host processes
            processpernode (int): number of process per node
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
