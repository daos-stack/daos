#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from telemetry_utils import TelemetryUtils


class TestWithTelemetry(TestWithServers):
    """Test container telemetry metrics.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super().__init__(*args, **kwargs)
        self.telemetry = None

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.telemetry = TelemetryUtils(
            self.get_dmg_command(), self.server_managers[0].hosts)

    def compare_lists(self, expected, actual, indent, prefix, description):
        """Compare two lists.

        Args:
            expected (list): expected list of items to compare
            actual (list): actual list of items to compare
            indent (int): number of spaces of indent
            prefix (str): string included at the beginning of the log entry
            description (str): description of the lists being compared

        Returns:
            list: any errors detected when comapring the two lists

        """
        errors = []
        self.log.info(
            "%s%s%s %s/%s %s", " " * indent, prefix,
            ": detected" if prefix else "Detected",
            len(actual), len(expected), description)
        difference = set(expected) - set(actual)
        self.log.info(
            "  %s%s%s between expected and actual: %s", " " * indent, prefix,
            ": difference" if prefix else "Difference", difference)
        symmetric_difference = set(expected) ^ set(actual)
        self.log.info(
            "  %s%s%s difference between expected and actual: %s", " " * indent,
            prefix, ": symmetric" if prefix else "Symmetric",
            symmetric_difference)
        if difference:
            errors.append(
                "Difference found in {}{}".format(
                    description, " on " + prefix if prefix else ""))
        if symmetric_difference:
            errors.append(
                "Symmetric difference found in {}{}".format(
                    description, " on " + prefix if prefix else ""))
        return errors

    def verify_telemetry_list(self):
        """Verify the  dmg telemetry metrics list command output."""
        # Define a list of expected telemetry metrics names
        expected = self.telemetry.get_all_server_metrics_names(
            self.server_managers[0])

        # List all of the telemetry metrics
        result = self.telemetry.list_metrics()

        # Verify the lists are detected for each server
        errors = self.compare_lists(
            list(result), self.server_managers[0].hosts, 0, "",
            "telemetry metrics list hosts")
        for host in result:
            errors.extend(
                self.compare_lists(
                    expected, result[host], 2, host, "telemetry metric names"))

        # errors = []
        # self.log.info(
        #     "Verifying telemetry metrics list for %s/%s hosts:",
        #     len(result), len(self.server_managers[0].hosts))
        # if sorted(result) != sorted(self.server_managers[0].hosts):
        #     msg = "Telemetry metrics names missing for hosts {}: {}".format(
        #         self.server_managers[0].hosts, result)
        #     self.log.error("  - %s", msg)
        #     errors.append(msg)
        # for host in result:
        #     self.log.info(
        #         "  %s: detected %s/%s telemetry metric names",
        #         host, len(result[host]), len(expected))
        #     difference = set(expected) - set(result[host])
        #     self.log.info(
        #         "  %s: difference between expected and actual: %s",
        #         host, difference)
        #     symmetric_difference = set(expected) ^ set(result[host])
        #     self.log.info(
        #         "  %s: symmetric difference between expected and actual: %s",
        #         host, symmetric_difference)
        #     if difference:
        #         errors.append(
        #             "Difference found in telemetry metrics list on {}".format(
        #                 host))
        #     if symmetric_difference:
        #         errors.append(
        #             "Symmetric difference found in telemetry metrics list on "
        #             "{}".format(host))
        if errors:
            self.fail("\n".join(errors))

        self.log.info("Test PASSED")
