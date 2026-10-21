"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from ClusterShell.NodeSet import NodeSet
from command_utils import ExecutableCommand
from command_utils_base import BasicParameter, FormattedParameter
from exception_utils import CommandFailure
from general_utils import get_log_file
from run_utils import run_remote


class DaosRacerCommand(ExecutableCommand):
    """Defines a object representing a daos_racer command."""

    def __init__(self, path, hosts, dmg=None, namespace="/run/daos_racer/*"):
        """Create a daos_racer command object.

        Args:
            path (str): path of the daos_racer command
            hosts (str/NodeSet): hosts on which to run the daos_racer command
            dmg (DmgCommand): a DmgCommand object used to obtain the
                configuration file and certificate
            namespace (str): yaml namespace (path to parameters). Defaults to "/run/daos_racer/*".
        """
        super().__init__(namespace, "daos_racer", path)
        if not isinstance(hosts, NodeSet):
            hosts = NodeSet(hosts)
        self._hosts = NodeSet(hosts)

        # Number of seconds to run
        self.runtime = FormattedParameter("-t {}", 60)
        self.pool_uuid = FormattedParameter("-p {}")
        self.cont_uuid = FormattedParameter("-c {}")

        if dmg:
            self.dmg_config = FormattedParameter("-n {}", dmg.yaml.filename)
            dmg.copy_certificates(get_log_file("daosCA/certs"), self._hosts)
            dmg.copy_configuration(self._hosts)

        # Optional timeout for running the daos_racer command.
        # This should be set greater than the 'runtime' value but less than the
        # avocado test timeout value to allow for proper cleanup.  Using a value
        # of None will result in no timeout being used.
        self.daos_racer_timeout = BasicParameter(None)

        # Include bullseye coverage file environment
        self.env["COVFILE"] = os.path.join(os.sep, "tmp", "test.cov")

        # Use a separate log file by default
        self.env["D_LOG_FILE"] = get_log_file(f"{self.command}.log")

    def get_str_param_names(self):
        """Get a sorted list of the names of the command attributes.

        Only include FormattedParameter class parameter values when building the
        command string, e.g. 'runtime'.

        Returns:
            list: a list of class attribute names used to define parameters
                for the command.

        """
        return self.get_attribute_names(FormattedParameter)

    def run(self, raise_exception=None):
        """Run the daos_racer command remotely.

        Args:
            raise_exception (bool, optional): whether or not to raise an exception if the command
                fails. This overrides the self.exit_status_exception
                setting if defined. Defaults to None.

        Raises:
            CommandFailure: if there is an error running the command

        """
        if raise_exception is None:
            raise_exception = self.exit_status_exception

        # Run daos_racer on the specified host
        self.log.info(
            "Running %s on %s with %s timeout",
            str(self), self._hosts,
            "no" if self.daos_racer_timeout.value is None
            else f"a {self.daos_racer_timeout.value}s")
        result = run_remote(
            self.log, self._hosts, self.with_exports, timeout=self.daos_racer_timeout.value)
        if not result.passed:
            if result.timeout:
                self.log.info("Stopping timed out daos_racer process on %s", result.timeout_hosts)
                run_remote(self.log, result.timeout_hosts, "pkill daos_racer", True)

            if raise_exception:
                raise CommandFailure(f"Error running '{self._command}'")
