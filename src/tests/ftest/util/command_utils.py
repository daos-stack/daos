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
from importlib import import_module
import re
import time
import signal

from avocado.utils import process

from command_utils_base import \
    CommandFailure, BasicParameter, NamedParameter, ObjectWithParameters, \
    CommandWithParameters, YamlParameters, FormattedParameter, \
    EnvironmentVariables
from general_utils import check_file_exists, stop_processes, get_log_file


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

        # A list of environment variable names to set and export prior to
        # running the command.  Values can be set via the get_environment()
        # method and included in the command string by the set_environment()
        # method.
        self._env_names = []

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

    def get_environment(self, manager, log_file=None):
        """Get the environment variables to export for the command.

        Args:
            manager (DaosServerManager): the job manager used to start
                daos_server from which the server config values can be obtained
                to set the required environment variables.
            log_file (str, optional): when specified overrides the default
                D_LOG_FILE value. Defaults to None.

        Returns:
            EnvironmentVariables: a dictionary of environment variable names and
                values to export prior to running daos_racer

        """
        env = EnvironmentVariables()
        for name in self._env_names:
            if name == "D_LOG_FILE":
                if not log_file:
                    log_file = "{}_daos.log".format(self.command)
                value = get_log_file(log_file)
            else:
                value = manager.get_environment_value(name)
            env[name] = value

        return env

    def set_environment(self, env):
        """Set the environment variables to export in the command string.

        Args:
            env (EnvironmentVariables): a dictionary of environment variable
                names and values to export prior to running the command
        """
        self._pre_command = env.get_export_str()


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


class SubProcessCommand(CommandWithSubCommand):
    """A class for a command run as a subprocess with a sub command.

    Example commands: daos_agent, daos_server
    """

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
        self.pattern = None
        self.pattern_count = 1
        self.pattern_timeout = BasicParameter(timeout, timeout)

    def get_str_param_names(self):
        """Get a sorted list of the names of the command attributes.

        Exclude the 'pattern_timeout' BasicParameter value from the command
        string as it is only used internally to the class.

        Returns:
            list: a list of class attribute names used to define parameters
                for the command.

        """
        names = self.get_param_names()
        names.remove("pattern_timeout")
        if self.sub_command_class is not None:
            index = names.index("sub_command")
            names[index] = "sub_command_class"
        return names

    def check_subprocess_status(self, sub_process):
        """Verify the status of the command started as a subprocess.

        Continually search the subprocess output for a pattern (self.pattern)
        until the expected number of patterns (self.pattern_count) have been
        found (typically one per host) or the timeout (self.pattern_timeout)
        is reached or the process has stopped.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command

        Returns:
            bool: whether or not the command progress has been detected

        """
        complete = True
        self.log.info(
            "Checking status of the %s command in %s with a %s second timeout",
            self._command, sub_process, self.pattern_timeout.value)

        if self.pattern is not None:
            detected = 0
            complete = False
            timed_out = False
            start = time.time()

            # Search for patterns in the subprocess output until:
            #   - the expected number of pattern matches are detected (success)
            #   - the time out is reached (failure)
            #   - the subprocess is no longer running (failure)
            while not complete and not timed_out and sub_process.poll() is None:
                output = sub_process.get_stdout()
                detected = len(re.findall(self.pattern, output))
                complete = detected == self.pattern_count
                timed_out = time.time() - start > self.pattern_timeout.value

            # Summarize results
            msg = "{}/{} '{}' messages detected in {}/{} seconds".format(
                detected, self.pattern_count, self.pattern,
                time.time() - start, self.pattern_timeout.value)

            if not complete:
                # Report the error / timeout
                self.log.info(
                    "%s detected - %s:\n%s",
                    "Time out" if timed_out else "Error",
                    msg,
                    sub_process.get_stdout())

                # Stop the timed out process
                if timed_out:
                    self.stop()
            else:
                # Report the successful start
                self.log.info(
                    "%s subprocess startup detected - %s", self._command, msg)

        return complete


class YamlCommand(SubProcessCommand):
    """Defines a sub-process command that utilizes a yaml configuration file.

    Example commands: daos_agent, daos_server, dmg
    """

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

    def create_yaml_file(self):
        """Create the yaml file with the current yaml file parameters.

        This should be called before running the daos command and after all the
        yaml file parameters have been defined.  Any updates to the yaml file
        parameter definitions would require calling this method before calling
        the daos command in order for them to have any effect.
        """
        if isinstance(self.yaml, YamlParameters):
            self.yaml.create_yaml()

    def set_config_value(self, name, value):
        """Set the yaml configuration parameter value.

        Args:
            name (str): name of the yaml configuration parameter
            value (object): value to set

        Returns:
            bool: if the attribute name was found and the value was set

        """
        status = False
        if isinstance(self.yaml, YamlParameters):
            status = self.yaml.set_value(name, value)
        return status

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


class SubprocessManager(object):
    """Defines an object that manages a sub process launched with orterun."""

    def __init__(self, command, manager="Orterun"):
        """Create a SubprocessManager object.

        Args:
            command (YamlCommand): command to manage as a subprocess
            manager (str, optional): the name of the JobManager class used to
                manage the YamlCommand defined through the "job" attribute.
                Defaults to "OpenMpi"
        """
        self.log = getLogger(__name__)

        # Define the JobManager class used to manage the command as a subprocess
        try:
            manager_module = import_module("job_manager_utils")
            manager_class = getattr(manager_module, manager)
        except (ImportError, AttributeError) as error:
            raise CommandFailure(
                "Invalid '{}' job manager class: {}".format(manager, error))
        self.manager = manager_class(command, subprocess=True)

        # Define the list of hosts that will execute the daos command
        self._hosts = []

        # Define the list of executable names to terminate in the kill() method
        self._exe_names = [self.manager.job.command]

    def __str__(self):
        """Get the complete manager command string.

        Returns:
            str: the complete manager command string

        """
        return str(self.manager)

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
        self._set_hosts(*value)

    def _set_hosts(self, hosts, path, slots):
        """Set the hosts used to execute the daos command.

        Defined as a private method to enable overriding the setter method.

        Args:
            hosts (list): list of hosts on which to run the command
            path (str): path in which to create the hostfile
            slots (int): number of slots per host to specify in the hostfile
        """
        self._hosts = hosts
        self.manager.assign_hosts(self._hosts, path, slots)
        self.manager.assign_processes(len(self._hosts))

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Use the yaml file paramter values to assign the server command and
        orterun command parameters.

        Args:
            test (Test): avocado Test object
        """
        # Get the parameters for the JobManager command parameters
        self.manager.get_params(test)

        # Get the values for the job parameters
        self.manager.job.get_params(test)

    def start(self):
        """Start the daos command.

        Raises:
            CommandFailure: if the daos command fails to start

        """
        # Create the yaml file for the daos command
        self.manager.job.create_yaml_file()

        # Start the daos command
        try:
            self.manager.run()
        except CommandFailure:
            # Kill the subprocess, anything that might have started
            self.kill()
            raise CommandFailure(
                "Failed to start {}.".format(str(self.manager.job)))

    def stop(self):
        """Stop the daos command."""
        self.manager.stop()

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
        if self._hosts and hasattr(self.manager.job, "yaml"):
            directory = self.get_user_file()
            status, nodes = check_file_exists(self._hosts, directory, user)
            if not status:
                raise CommandFailure(
                    "{}: Server missing socket directory {} for user {}".format(
                        nodes, directory, user))

    def set_config_value(self, name, value):
        """Set the yaml configuration parameter value.

        Args:
            name (str): name of the yaml configuration parameter
            value (object): value to set

        Returns:
            bool: if the attribute name was found and the value was set

        """
        status = False
        if self.manager.job and hasattr(self.manager.job, "set_config_value"):
            status = self.manager.job.set_config_value(name, value)
        return status

    def get_config_value(self, name):
        """Get the value of the yaml configuration parameter name.

        Args:
            name (str): name of the yaml configuration parameter from which to
                get the value

        Returns:
            object: the yaml configuration parameter value or None

        """
        value = None
        if self.manager.job and hasattr(self.manager.job, "get_config_value"):
            value = self.manager.job.get_config_value(name)
        return value

    def get_user_file(self):
        """Get the file defined in the yaml file that must be owned by the user.

        Returns:
            str: file defined in the yaml file that must be owned by the user

        """
        return self.get_config_value("socket_dir")
