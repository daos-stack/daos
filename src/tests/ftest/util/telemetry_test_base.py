#!/usr/bin/python3
"""
(C) Copyright 2021-2022 Intel Corporation.

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
            list: any errors detected when comparing the two lists

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

    def verify_telemetry_list(self, with_pools=False):
        """Verify the  dmg telemetry metrics list command output."""
        # Define a list of expected telemetry metrics names
        expected = self.telemetry.get_all_server_metrics_names(
            self.server_managers[0], with_pools=with_pools)

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
        if errors:
            self.fail("\n".join(errors))

        self.log.info("Test PASSED")

    def get_min_max_mean_stddev(self, prefix, total_targets, targets_per_rank):
        """Get four lists of min, max, mean, stddev.

        Sample get_pool_metrics output.
        {
            'engine_io_ops_dkey_enum_latency_min': {
                'wolf-31': {
                    '0': {
                        '0': 6,
                        '1': 13,
                        '2': 7,
                        '3': 6,
                        '4': 6,
                        '5': 8,
                        '6': 10,
                        '7': 12
                    }
                },
                'wolf-32': {
                    '1': {
                        '0': 10,
                        '1': 9,
                        '2': 7,
                        '3': 10,
                        '4': 18,
                        '5': 9,
                        '6': 6,
                        '7': 11
                    }
                }
            },
            ...

        Args:
            prefix (str): Metrics prefix for the metric that has min, max,
                mean, or stddev at the end.
            total_targets (int): Total number of targets in all the ranks and
                hosts in the output.
            targets_per_rank (int): Number of target per rank.

        Returns:
            dict: min, max, mean, stddev from each target in a dict.
                {
                    "min": [0, 1, 2, 3, 4, 5, 6, 7...],
                    "max": [0, 1, 2, 3, 4, 5, 6, 7...],
                    "mean": [0, 1, 2, 3, 4, 5, 6, 7...],
                    "stddev": [0, 1, 2, 3, 4, 5, 6, 7...]
                }
                Note that all the stats should have the same rank order for the
                verification to work.

        """
        output = {}

        for stats_value in ["min", "max", "mean", "stddev"]:
            specific_metrics = [prefix + stats_value]
            pool_out = self.telemetry.get_pool_metrics(
                specific_metrics=specific_metrics)
            self.log.info("pool_out = %s", pool_out)

            values = [0] * total_targets
            offset = 0

            for rank_to_dicts in pool_out[specific_metrics[0]].values():
                for target_to_val in rank_to_dicts.values():
                    for target, value in target_to_val.items():
                        values[int(target) + offset * targets_per_rank] = value

                    offset += 1

            output[stats_value] = values

        self.log.info("output for %s = %s", prefix, output)
        return output

    @staticmethod
    def verify_stats(enum_metrics, metric_prefix, test_latency):
        """Verify the statistical correctness of the min, max, mean, stddev.

        See get_min_max_mean_stddev() for sample.

        This method assumes that the rank order is consistent across all the
        stats. For example, the sample output in get_min_max_mean_stddev() has
        rank 0 first, then rank 1, which fills up the enum_metrics from first
        target of rank 0 to last target of rank 1. In this case, all other
        stats should have the same rank order, which is rank 0 first then
        rank 1, for the verification to work.

        Args:
            enum_metrics (dict): get_min_max_mean_stddev() output.
            metric_prefix (str): Metric prefix. For example,
                engine_io_ops_dkey_enum_active_
            test_latency (bool): Whether this validation is for latency.

        Returns:
            list: Errors.

        """
        errors = []

        num_targets = len(enum_metrics["min"])

        min_list = enum_metrics["min"]
        max_list = enum_metrics["max"]
        mean_list = enum_metrics["mean"]
        stddev_list = enum_metrics["stddev"]

        for tgt in range(num_targets):
            if max_list[tgt] == 0:
                continue

            if test_latency:
                if min_list[tgt] == 0:
                    errors.append(
                        "{}min is 0 at target {}!".format(metric_prefix, tgt))
            else:
                if min_list[tgt] != 0:
                    errors.append(
                        "{}min is NOT 0 at target {}!".format(
                            metric_prefix, tgt))

            if mean_list[tgt] < min_list[tgt] or max_list[tgt] < mean_list[tgt]:
                errors.append(
                    "{}mean is invalid at target {}!".format(
                        metric_prefix, tgt))

            if stddev_list[tgt] > max_list[tgt] - min_list[tgt]:
                errors.append(
                    "{}stddev is invalid at target {}!".format(
                        metric_prefix, tgt))

        return errors

    @staticmethod
    def sum_values(metric_out):
        """Sum the metric values.

        Sample metric_out.
        {
            'wolf-31': {
                '0': {
                    '0': 6,
                    '1': 13,
                    '2': 7,
                    '3': 6,
                    '4': 6,
                    '5': 8,
                    '6': 10,
                    '7': 12
                }
            },
            'wolf-32': {
                '1': {
                    '0': 10,
                    '1': 9,
                    '2': 7,
                    '3': 10,
                    '4': 18,
                    '5': 9,
                    '6': 6,
                    '7': 11
                }
            }
        }

        Args:
            metric_out (dict): Values to sum.

        Returns:
            int: Sum.

        """
        total = 0

        for rank_to_dict in metric_out.values():
            for target_to_val in rank_to_dict.values():
                for value in target_to_val.values():
                    total += value

        return total

    def get_aggregate_metrics(self, metrics=None):
        """"Get aggregate metrics data.

        Args:
            metrics (list, optional): list of specific pool metrics to aggregate.
                Defaults to None.

        Returns:
            dict: mapping of metrics to hosts to ranks to aggregate values.
                I.e., {metric: {host: {rank: sum}}}

        """
        agg = {}
        telemetry_data = self.telemetry.get_pool_metrics(metrics)

        for metric, metric_data in telemetry_data.items():
            agg[metric] = {}
            for host, host_data in metric_data.items():
                agg[metric][host] = {}
                for rank, rank_data in host_data.items():
                    agg[metric][host][rank] = sum(rank_data.values())

        return agg
