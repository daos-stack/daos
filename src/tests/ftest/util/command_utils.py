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
import re
import signal
import time

from avocado.utils import process

from command_utils_base import \
    CommandFailure, BasicParameter, NamedParameter, ObjectWithParameters, \
    CommandWithParameters, FormattedParameter


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
            # Send a SIGTERM to the stop the subprocess and if it is still
            # running after 5 seconds send a SIGKILL and report an error
            signal_list = [signal.SIGTERM, signal.SIGKILL]

            # # Turn off verbosity to keep the logs clean as the server stops
            # self._process.verbose = False

            # Send signals while the process is still running
            while self._process.poll() is None and signal_list:
                self.display_subprocess_state()
                self._process.send_signal(signal_list.pop(0))
                if signal_list:
                    time.sleep(5)

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

    def display_subprocess_state(self):
        """Display the state of the subprocess."""
        if self._process is not None:
            command = "pstree -pls {}".format(self._process.get_pid())
            result = process.run(command, 5, True, True, "combined", True)
            self.log.debug(
                "Processes still running:\n  %s",
                "  \n".join(result.stdout_text.splitlines()))


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
