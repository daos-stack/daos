#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines
from logging import getLogger
from datetime import datetime
from getpass import getuser
import re
import time
import signal
import os
import json

from avocado.utils import process
from ClusterShell.NodeSet import NodeSet

from command_utils_base import \
    CommandFailure, BasicParameter, ObjectWithParameters, \
    CommandWithParameters, YamlParameters, EnvironmentVariables, LogParameter
from general_utils import check_file_exists, get_log_file, \
    run_command, DaosTestError, get_job_manager_class, create_directory, \
    distribute_files, change_file_owner, get_file_listing, run_pcmd, \
    get_subprocess_stdout


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
        super().__init__(namespace, command, path)
        self._process = None
        self.run_as_subprocess = subprocess
        self.timeout = None
        self.exit_status_exception = True
        self.output_check = "both"
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
        value = super().__str__()
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
                self.output_check, env=self.env)

        except DaosTestError as error:
            # Command failed or possibly timed out
            raise CommandFailure from error

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
            # Send a SIGTERM to stop the subprocess and if it is still
            # running after 5 seconds give it another try. If that doesn't
            # stop the process send a SIGKILL and report an error.
            # Sending 2 SIGTERM signals is a known issue based on
            # DAOS-6850.
            signal_list = [signal.SIGTERM, signal.SIGTERM, signal.SIGKILL]

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
                    start = time.time()
                    elapsed = 0
                    # pylint: disable=protected-access
                    while self._process._popen.poll() is None and elapsed < 5:
                        time.sleep(0.01)
                        elapsed = time.time() - start
                    self.log.info(
                        "Waited %.2f, saved %.2f", elapsed, 5 - elapsed)

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
                r"\d+\s+([DRSTtWXZ<NLsl+]+)\s+\d+", result.stdout_text)
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
        return self.parse_output(result.stdout_text, regex_method)

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
        super().__init__(namespace, command, path)

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

        self.json = None

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
        super().get_params(test)
        self.get_sub_command_class()
        if (self.sub_command_class is not None and
                hasattr(self.sub_command_class, "get_params")):
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
            self.result = super().run()
        except CommandFailure as error:
            raise CommandFailure(
                "<{}> command failed: {}".format(
                    self.command, error)) from error
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
        for name, value in list(kwargs.items()):
            getattr(this_command, name).value = value

        # Issue the command and store the command result
        return self.run()

    def _get_json_result(self, sub_command_list=None, **kwargs):
        """Wrap the base _get_result method to force JSON output.

        Args:
            sub_command_list (list): List of subcommands.
            kwargs (dict): Parameters for the command.
        """
        if self.json is None:
            raise CommandFailure(
                f"The {self.command} command doesn't have json option defined!")
        prev_json_val = self.json.value
        self.json.update(True)
        prev_output_check = self.output_check
        self.output_check = "both"
        try:
            self._get_result(sub_command_list, **kwargs)
        finally:
            self.json.update(prev_json_val)
            self.output_check = prev_output_check
        return json.loads(self.result.stdout)


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
        super().__init__(namespace, command, path, True)

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
                output = get_subprocess_stdout(sub_process)
                detected = len(re.findall(self.pattern, output))
                complete = detected == self.pattern_count
                timed_out = time.time() - start > self.pattern_timeout.value

            # Summarize results
            msg = "{}/{} '{}' messages detected in".format(
                detected, self.pattern_count, self.pattern)
            runtime = "{}/{} seconds".format(
                time.time() - start, self.pattern_timeout.value)

            if not complete:
                # Report the error / timeout
                reason = "ERROR detected"
                details = ""
                if timed_out:
                    reason = "TIMEOUT detected, exceeded {} seconds".format(
                        self.pattern_timeout.value)
                    runtime = "{} seconds".format(time.time() - start)
                if not self.verbose:
                    # Include the stdout if verbose is not enabled
                    details = ":\n{}".format(get_subprocess_stdout(sub_process))
                self.log.info("%s - %s %s%s", reason, msg, runtime, details)
                if timed_out:
                    self.log.debug(
                        "If needed the %s second timeout can be adjusted via "
                        "the 'pattern_timeout' test yaml parameter under %s",
                        self.pattern_timeout.value, self.namespace)

                # Stop the timed out process
                if timed_out:
                    self.stop()
            else:
                # Report the successful start
                self.log.info(
                    "%s subprocess startup detected - %s %s",
                    self._command, msg, runtime)

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
        super().__init__(namespace, command, path, timeout)

        # Command configuration yaml file
        self.yaml = yaml_cfg

        # Optional attribute used to define a location where the configuration
        # file data will be written prior to copying the file to the hosts using
        # the assigned filename
        self.temporary_file = None
        self.temporary_file_hosts = None

        # Owner of the certificate files
        self.certificate_owner = getuser()

    @property
    def service_name(self):
        """Get the systemctl service name for this command.

        Returns:
            str: systemctl service name

        """
        return ".".join((self._command, "service"))

    def get_params(self, test):
        """Get values for the daos command and its yaml config file.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)
        if self.yaml is not None and hasattr(self.yaml, "get_params"):
            self.yaml.get_params(test)

    def create_yaml_file(self):
        """Create the yaml file with the current yaml file parameters.

        This should be called before running the daos command and after all the
        yaml file parameters have been defined.  Any updates to the yaml file
        parameter definitions would require calling this method before calling
        the daos command in order for them to have any effect.

        Raises:
            CommandFailure: if there is an error copying the configuration file.
                Can only be raised if the self.temporary_file and
                self.temporary_file_hosts attributes are defined.

        """
        if self.yaml is not None and hasattr(self.yaml, "create_yaml"):
            if self.yaml.create_yaml(self.temporary_file):
                self.copy_configuration(self.temporary_file_hosts)

    def set_config_value(self, name, value):
        """Set the yaml configuration parameter value.

        Args:
            name (str): name of the yaml configuration parameter
            value (object): value to set

        Returns:
            bool: if the attribute name was found and the value was set

        """
        status = False
        if self.yaml is not None and hasattr(self.yaml, "set_value"):
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
        if self.yaml is not None and hasattr(self.yaml, "get_value"):
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
        return super().run()

    def copy_certificates(self, source, hosts):
        """Copy certificates files from the source to the destination hosts.

        Args:
            source (str): source of the certificate files.
            hosts (list): list of the destination hosts.
        """
        names = set()
        yaml = self.yaml
        while yaml is not None and hasattr(yaml, "other_params"):
            if hasattr(yaml, "get_certificate_data"):
                self.log.debug("Copying certificates for %s:", self._command)
                data = yaml.get_certificate_data(
                    yaml.get_attribute_names(LogParameter))
                for name in data:
                    create_directory(
                        hosts, name, verbose=False, raise_exception=False)
                    for file_name in data[name]:
                        src_file = os.path.join(source, file_name)
                        dst_file = os.path.join(name, file_name)
                        self.log.debug("  %s -> %s", src_file, dst_file)
                        result = distribute_files(
                            hosts, src_file, dst_file, mkdir=False,
                            verbose=False, raise_exception=False, sudo=True,
                            owner=self.certificate_owner)
                        if result.exit_status != 0:
                            self.log.info(
                                "    WARNING: %s copy failed on %s:\n%s",
                                dst_file, hosts, result)
                    names.add(name)
            yaml = yaml.other_params

        # debug to list copy of cert files
        if names:
            self.log.debug(
                "Copied certificates for %s (in %s):",
                self._command, ", ".join(names))
            for line in get_file_listing(hosts, names).stdout_text.splitlines():
                self.log.debug("  %s", line)

    def copy_configuration(self, hosts):
        """Copy the yaml configuration file to the hosts.

        If defined self.temporary_file is copied to hosts using the path/file
        specified by the YamlParameters.filename.

        Args:
            hosts (list): hosts to which to copy the configuration file.

        Raises:
            CommandFailure: if there is an error copying the configuration file

        """
        if self.yaml is not None and hasattr(self.yaml, "filename"):
            if self.temporary_file and hosts:
                self.log.info(
                    "Copying %s yaml configuration file to %s on %s",
                    self.temporary_file, self.yaml.filename, hosts)
                try:
                    distribute_files(
                        hosts, self.temporary_file, self.yaml.filename,
                        verbose=False, sudo=True)
                except DaosTestError as error:
                    raise CommandFailure(
                        "ERROR: Copying yaml configuration file to {}: "
                        "{}".format(hosts, error)) from error

    def verify_socket_directory(self, user, hosts):
        """Verify the domain socket directory is present and owned by this user.

        Args:
            user (str): user to verify has ownership of the directory
            hosts (list): list of hosts on which to verify the directory exists

        Raises:
            CommandFailure: if the socket directory does not exist or is not
                owned by the user and could not be created

        """
        if self.yaml is not None:
            directory = self.get_user_file()
            self.log.info(
                "Verifying %s socket directory: %s", self.command, directory)
            status, nodes = check_file_exists(hosts, directory, user)
            if not status:
                self.log.info(
                    "%s: creating socket directory %s for user %s on %s",
                    self.command, directory, user, nodes)
                try:
                    create_directory(nodes, directory, sudo=True)
                    change_file_owner(nodes, directory, user, user, sudo=True)
                except DaosTestError as error:
                    raise CommandFailure(
                        "{}: error setting up missing socket directory {} for "
                        "user {} on {}:\n{}".format(
                            self.command, directory, user, nodes,
                            error)) from error

    def get_user_file(self):
        """Get the file defined in the yaml file that must be owned by the user.

        Returns:
            str: file defined in the yaml file that must be owned by the user

        """
        return self.get_config_value("socket_dir")


class SubprocessManager():
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
        self.manager = get_job_manager_class(manager, command, True)
        self._id = self.manager.job.command.replace("daos_", "")

        # Define the list of hosts that will execute the daos command
        self._hosts = []

        # The socket directory verification is not required with systemctl
        self._verify_socket_dir = manager != "Systemctl"

        # An internal dictionary used to define the expected states of each
        # job process. It will be populated when any of the following methods
        # are called:
        #   - start()
        #   - verify_expected_states(set_expected=True)
        # Individual states may also be updated by calling the
        # update_expected_states() method. This is required to avoid any errors
        # being raised during tearDown() if a test variant intentional affects
        # the state of a job process.
        self._expected_states = {}

        # States for verify_expected_states()
        self._states = {
            "all": [
                "active", "inactive", "activating", "deactivating", "failed",
                "unknown"],
            "running": ["active"],
            "stopped": ["inactive", "deactivating", "failed", "unknown"],
            "errored": ["failed"],
        }

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
        self._hosts = list(hosts)
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
        self.manager.job.temporary_file_hosts = self._hosts
        self.manager.job.create_yaml_file()

        # Start the daos command
        try:
            self.manager.run()
        except CommandFailure as error:
            # Kill the subprocess, anything that might have started
            self.manager.kill()
            raise CommandFailure(
                "Failed to start {}.".format(str(self.manager.job))) from error
        finally:
            # Define the expected states for each rank
            self._expected_states = self.get_current_state()

    def stop(self):
        """Stop the daos command."""
        self.manager.stop()

    def verify_socket_directory(self, user):
        """Verify the domain socket directory is present and owned by this user.

        Args:
            user (str): user to verify has ownership of the directory

        Raises:
            CommandFailure: if the socket directory does not exist or is not
                owned by the user

        """
        if self._hosts and self._verify_socket_dir:
            self.manager.job.verify_socket_directory(user, self._hosts)

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

    def get_current_state(self):
        """Get the current state of the daos_server ranks.

        Returns:
            dict: dictionary of server rank keys, each referencing a dictionary
                of information containing at least the following information:
                    {"host": <>, "uuid": <>, "state": <>}
                This will be empty if there was error obtaining the dmg system
                query output.

        """
        data = {}
        ranks = {host: rank for rank, host in enumerate(self._hosts)}
        if not self._verify_socket_dir:
            command = "systemctl is-active {}".format(
                self.manager.job.service_name)
        else:
            command = "pgrep {}".format(self.manager.job.command)
        results = run_pcmd(self._hosts, command, 30)
        for result in results:
            for node in result["hosts"]:
                # expecting single line output from run_pcmd
                stdout = result["stdout"][-1] if result["stdout"] else "unknown"
                data[ranks[node]] = {"host": node, "uuid": "-", "state": stdout}
        return data

    def update_expected_states(self, ranks, state):
        """Update the expected state of the specified job rank.

        Args:
            ranks (object): job ranks to update. Can be a single rank (int),
                multiple ranks (list), or all the ranks (None).
            state (object): new state to assign as the expected state of this
                rank. Can be a str or a list of str.
        """
        if ranks is None:
            ranks = list(self._expected_states.keys())
        elif not isinstance(ranks, (list, tuple)):
            ranks = [ranks]

        for rank in ranks:
            if rank in self._expected_states:
                self.log.info(
                    "Updating the expected state for rank %s on %s: %s -> %s",
                    rank, self._expected_states[rank]["host"],
                    self._expected_states[rank]["state"], state)
                self._expected_states[rank]["state"] = state

    def verify_expected_states(self, set_expected=False):
        """Verify that the expected job process states match the current states.

        Args:
            set_expected (bool, optional): option to update the expected job
                process states to the current states prior to verification.
                Defaults to False.

        Returns:
            dict: a dictionary of whether or not any of the job process states
                were not 'expected' (which should warrant an error) and whether
                or not the job process require a 'restart' (either due to any
                unexpected states or because at least one job process was no
                longer found to be running)

        """
        status = {"expected": True, "restart": False}
        show_log_hosts = []

        # Get the current state of each job process
        current_states = self.get_current_state()
        if set_expected:
            # Assign the expected states to the current job process states
            self.log.info(
                "<%s> Assigning expected %s states.",
                self._id.upper(), self._id)
            self._expected_states = current_states.copy()

        # Verify the expected states match the current states
        self.log.info(
            "<%s> Verifying %s states: group=%s, hosts=%s",
            self._id.upper(), self._id, self.get_config_value("name"),
            NodeSet.fromlist(self._hosts))
        if current_states:
            log_format = "  %-4s  %-15s  %-36s  %-22s  %-14s  %s"
            self.log.info(
                log_format,
                "Rank", "Host", "UUID", "Expected State", "Current State",
                "Result")
            self.log.info(
                log_format,
                "-" * 4, "-" * 15, "-" * 36, "-" * 22, "-" * 14, "-" * 6)

            # Verify that each expected rank appears in the current states
            for rank in sorted(self._expected_states):
                current_host = self._expected_states[rank]["host"]
                expected = self._expected_states[rank]["state"]
                if isinstance(expected, (list, tuple)):
                    expected = [item.lower() for item in expected]
                else:
                    expected = [expected.lower()]
                try:
                    current_rank = current_states.pop(rank)
                    current = current_rank["state"].lower()
                except KeyError:
                    current = "not detected"

                # Check if the job's expected state matches the current state
                result = "PASS" if current in expected else "RESTART"
                status["expected"] &= current in expected

                # Restart all job processes if the expected rank is not running
                if current not in self._states["running"]:
                    status["restart"] = True
                    result = "RESTART"

                # Keep track of any server in the errored state or in an
                # unexpected state in order to display its log
                if (current in self._states["errored"] or
                        current not in expected):
                    if current_host not in show_log_hosts:
                        show_log_hosts.append(current_host)

                self.log.info(
                    log_format, rank, current_host,
                    self._expected_states[rank]["uuid"], "|".join(expected),
                    current, result)

        elif not self._expected_states:
            # Expected states are populated as part of start() procedure,
            # so if it is empty there was an error starting the job processes.
            self.log.info(
                "  Unable to obtain current %s state.  Undefined expected %s "
                "states due to a failure starting the %s.",
                self._id, self._id, self._id,)
            status["restart"] = True

        else:
            # Any failure to obtain the current rank information is an error
            self.log.info(
                "  Unable to obtain current %s state.  If the %ss are "
                "not running this is expected.", self._id, self._id)

            # Do not report an error if all servers are expected to be stopped
            all_stopped = bool(self._expected_states)
            for rank in sorted(self._expected_states):
                states = self._expected_states[rank]["state"]
                if not isinstance(states, (list, tuple)):
                    states = [states]
                if "stopped" not in [item.lower() for item in states]:
                    all_stopped = False
                    break
            if all_stopped:
                self.log.info("  All %s are expected to be stopped.", self._id)
                status["restart"] = True
            else:
                status["expected"] = False

        # Any unexpected state detected warrants a restart of all job processes
        if not status["expected"]:
            status["restart"] = True

        # Set the verified timestamp
        if set_expected and hasattr(self.manager, "timestamps"):
            self.manager.timestamps["verified"] = datetime.now().strftime(
                "%Y-%m-%d %H:%M:%S")

        # Dump the server logs for any identified server
        if show_log_hosts:
            self.log.info(
                "<SERVER> logs for ranks in the errored state since start "
                "detection or detected in an unexpected state")
            if hasattr(self.manager, "dump_logs"):
                self.manager.dump_logs(show_log_hosts)

        return status


class SystemctlCommand(ExecutableCommand):
    # pylint: disable=too-few-public-methods
    """Defines an object representing the systemctl command."""

    def __init__(self):
        """Create a SystemctlCommand object."""
        super().__init__(
            "/run/systemctl/*", "systemctl", subprocess=False)
        self.sudo = True

        self.unit_command = BasicParameter(None)
        self.service = BasicParameter(None)

    def get_str_param_names(self):
        """Get a sorted list of the names of the command attributes.

        Ensure the correct order of the attributes for the systemctl command,
        e.g.:
            systemctl <unit_command> <service>

        Returns:
            list: a list of class attribute names used to define parameters
                for the command.

        """
        return list(
            reversed(super().get_str_param_names()))
