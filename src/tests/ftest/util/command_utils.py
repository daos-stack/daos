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
import os

from avocado.utils import process

from command_utils_base import \
    CommandFailure, BasicParameter, ObjectWithParameters, \
    CommandWithParameters, YamlParameters, EnvironmentVariables, LogParameter
from general_utils import check_file_exists, stop_processes, get_log_file, \
    run_command, DaosTestError


class ExecutableCommand(CommandWithParameters):
    """A class for command with parameters."""

    # A list of regular expressions for each class method that produces a
    # CmdResult object.  Used by the self.get_output() method to return specific
    # values from the standard output yielded by the method.
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

        # Define a list of executable names associated with the command. This
        # list is used to generate the 'command_regex' property, which can be
        # used to check on the progress or terminate the command.
        self._exe_names = [self.command]

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

    @property
    def command_regex(self):
        """Get the regular expression to use to search for the command.

        Typical use would include combining with pgrep to verify a subprocess
        is running.

        Returns:
            str: regular expression to use to search for the command

        """
        return "'({})'".format("|".join(self._exe_names))

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
        try:
            # Block until the command is complete or times out
            return run_command(
                command, self.timeout, self.verbose, self.exit_status_exception,
                "combined", env=self.env)

        except DaosTestError as error:
            # Command failed or possibly timed out
            raise CommandFailure(error)

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
                "shell": False,
                "env": self.env,
                "sudo": self.sudo,
            }
            self._process = process.SubProcess(**kwargs)
            self._process.start()

            # Determine if the command has launched correctly using its
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
            # Send a SIGTERM to the stop the subprocess and if it is still
            # running after 5 seconds send a SIGKILL and report an error
            signal_list = [signal.SIGTERM, signal.SIGKILL]

            # Turn off verbosity to keep the logs clean as the server stops
            self._process.verbose = False

            # Send signals while the process is still running
            state = None
            while self._process.poll() is None and signal_list:
                signal_to_send = signal_list.pop(0)
                msg = "before sending signal {}".format(signal_to_send)
                state = self.get_subprocess_state(msg)
                self.log.info(
                    "Sending signal %s to %s (state=%s)", str(signal_to_send),
                    self._command, str(state))
                self._process.send_signal(signal_to_send)
                if signal_list:
                    time.sleep(5)

            if not signal_list:
                if state and (len(state) > 1 or state[0] not in ("D", "Z")):
                    # Indicate an error if the process required a SIGKILL and
                    # either multiple processes were still found running or the
                    # parent process was in any state except uninterruptible
                    # sleep (D) or zombie (Z).
                    raise CommandFailure("Error stopping '{}'".format(self))

            self.log.info("%s stopped successfully", self.command)
            self._process = None

    def wait(self):
        """Wait for the sub process to complete.

        Returns:
            int: return code of process

        """
        retcode = 0
        if self._process is not None:
            try:
                retcode = self._process.wait()
            except OSError as error:
                self.log.error("Error while waiting %s", error)
                retcode = 255

        return retcode

    def get_subprocess_state(self, message=None):
        """Display the state of the subprocess.

        Args:
            message (str, optional): additional text to include in output.
                Defaults to None.

        Returns:
            list: a list of process states for the process found associated with
                the subprocess pid.

        """
        state = None
        if self._process is not None:
            self.log.debug(
                "%s processes still running%s:", self.command,
                " {}".format(message) if message else "")
            command = "/usr/bin/ps --forest -o pid,stat,time,cmd {}".format(
                self._process.get_pid())
            result = process.run(command, 10, True, True, "combined")

            # Get the state of the process from the output
            state = re.findall(
                r"\d+\s+([DRSTtWXZ<NLsl+]+)\s+\d+", result.stdout)
        return state

    def get_output(self, method_name, regex_method=None, **kwargs):
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

        # Run the command
        result = method(**kwargs)
        if not isinstance(result, process.CmdResult):
            raise CommandFailure(
                "{}() did not return a CmdResult".format(method_name))

        # Parse the output and return
        if not regex_method:
            regex_method = method_name
        return self.parse_output(result.stdout, regex_method)

    def parse_output(self, stdout, regex_method):
        """Parse output using findall() with supplied 'regex_method' as pattern.

        Args:
            stdout (str): output to parse
            regex_method (str): name of the method regex to use

        Raises:
            CommandFailure: if there is an error finding the method's regex
                pattern.

        Returns:
            list: a list of strings obtained from the method's output parsed
                through its regex

        """
        if regex_method not in self.METHOD_REGEX:
            raise CommandFailure(
                "No pattern regex defined for '{}()'".format(regex_method))
        return re.findall(self.METHOD_REGEX[regex_method], stdout)

    def update_env_names(self, new_names):
        """Update environment variable names to export for the command.

        Args:
            env_names (list): list of environment variable names to add to
                existing self._env_names variable.
        """
        self._env_names.extend(new_names)

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
                values to export.

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
        self.env = env.copy()


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
        self.sub_command = BasicParameter(
            None, yaml_key="{}_sub_command".format(self._command))

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

        # Define an attribute to store the CmdResult from the last run() call.
        # A CmdResult object has the following properties:
        #   command         - command string
        #   exit_status     - exit_status of the command
        #   stdout          - the stdout
        #   stderr          - the stderr
        #   duration        - command execution time
        #   interrupted     - whether the command completed within timeout
        #   pid             - command's pid
        self.result = None

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

    def run(self):
        """Run the command and assign the 'result' attribute.

        Raises:
            CommandFailure: if there is an error running the command and the
                CommandWithSubCommand.exit_status_exception attribute is set to
                True.

        Returns:
            CmdResult: a CmdResult object containing the results of the command
                execution.

        """
        try:
            self.result = super(CommandWithSubCommand, self).run()
        except CommandFailure as error:
            raise CommandFailure(
                "<{}> command failed: {}".format(self.command, error))
        return self.result

    def _get_result(self, sub_command_list=None, **kwargs):
        """Get the result from running the command with the defined arguments.

        The optional sub_command_list and kwargs are used to define the command
        that will be executed.  If they are excluded, the command will be run as
        it currently defined.

        Note: the returned CmdResult is also stored in the self.result
        attribute as part of the self.run() call.

        Args:
            sub_command_list (list, optional): a list of sub commands used to
                define the command to execute. Defaults to None, which will run
                the command as it is currently defined.

        Raises:
            CommandFailure: if there is an error running the command and the
                CommandWithSubCommand.exit_status_exception attribute is set to
                True.

        Returns:
            CmdResult: a CmdResult object containing the results of the command
                execution.

        """
        # Set the subcommands
        this_command = self
        if sub_command_list is not None:
            for sub_command in sub_command_list:
                this_command.set_sub_command(sub_command)
                this_command = this_command.sub_command_class

        # Set the sub-command arguments
        for name, value in kwargs.items():
            getattr(this_command, name).value = value

        # Issue the command and store the command result
        return self.run()


class SubProcessCommand(CommandWithSubCommand):
    """A class for a command run as a subprocess with a sub command.

    Example commands: daos_agent, daos_server
    """

    def __init__(self, namespace, command, path="", timeout=10):
        """Create a SubProcessCommand object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            path (str, optional): path to location of command binary file.
                Defaults to "".
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 10 seconds.
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

    def __init__(self, namespace, command, path="", yaml_cfg=None, timeout=10):
        """Create a YamlCommand command object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            yaml_cfg (YamlParameters, optional): yaml configuration parameters.
                Defaults to None.
            path (str, optional): path to location of daos command binary.
                Defaults to ""
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 10 seconds.
        """
        super(YamlCommand, self).__init__(namespace, command, path, timeout)

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

    def run(self):
        """Run the command and assign the 'result' attribute.

        Ensure the yaml file is updated with the current attributes before
        executing the command.

        Raises:
            CommandFailure: if there is an error running the command and the
                CommandWithSubCommand.exit_status_exception attribute is set to
                True.

        Returns:
            CmdResult: a CmdResult object containing the results of the command
                execution.

        """
        if self.yaml:
            self.create_yaml_file()
        return super(YamlCommand, self).run()

    def copy_certificates(self, source, hosts):
        """Copy certificates files from the source to the destination hosts.

        Args:
            source (str): source of the certificate files.
            hosts (list): list of the destination hosts.
        """
        yaml = self.yaml
        while isinstance(yaml, YamlParameters):
            if hasattr(yaml, "get_certificate_data"):
                data = yaml.get_certificate_data(
                    yaml.get_attribute_names(LogParameter))
                for name in data:
                    run_command(
                        "clush -S -v -w {} /usr/bin/mkdir -p {}".format(
                            ",".join(hosts), name),
                        verbose=False)
                    for file_name in data[name]:
                        src_file = os.path.join(source, file_name)
                        dst_file = os.path.join(name, file_name)
                        result = run_command(
                            "clush -S -v -w {} --copy {} --dest {}".format(
                                ",".join(hosts), src_file, dst_file),
                            raise_exception=False, verbose=False)
                        if result.exit_status != 0:
                            self.log.info(
                                "WARNING: failure copying '%s' to '%s' on %s",
                                src_file, dst_file, hosts)

                    # debug to list copy of cert files
                    run_command(
                        "clush -S -v -w {} /usr/bin/ls -la {}".format(
                            ",".join(hosts), name))
            yaml = yaml.other_params


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

        Use the yaml file parameter values to assign the server command and
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
        """Forcibly terminate any sub process running on hosts."""
        regex = self.manager.job.command_regex
        result = stop_processes(self._hosts, regex)
        if 0 in result and len(result) == 1:
            print(
                "No remote {} processes killed (none found), done.".format(
                    regex))
        else:
            print(
                "***At least one remote {} process needed to be killed! Please "
                "investigate/report.***".format(regex))

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
