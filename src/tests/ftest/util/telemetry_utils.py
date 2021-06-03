#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
from ClusterShell.NodeSet import NodeSet


class TelemetryUtils():
    """Defines a object used to verify telemetry information."""

    def __init__(self, dmg, servers):
        """Create a TelemetryUtils object.

        Args:
            dmg (DmgCommand): [description]
            servers (list): [description]
        """
        self.log = getLogger(__name__)
        self.dmg = dmg
        self.hosts = NodeSet.fromlist(servers)

    def list_metrics(self):
        """List the available metrics for each host.

        Returns:
            dict: a dictionary of host keys linked to a list of metric names

        """
        info = {}
        self.log.info("Listing telemetry metrics from %s", self.hosts)
        for host in self.hosts:
            data = self.dmg.telemetry_metrics_list(host=host)
            info[host] = []
            if "response" in data:
                if "available_metric_sets" in data["response"]:
                    for entry in data["response"]["available_metric_sets"]:
                        if "name" in entry:
                            info[host].append(entry["name"])
        return info

    def get_metrics(self, name):
        """Obtain the specified metric informaton for each host.

        Args:
            name (str): Comma-separated list of metric names to query.

        Returns:
            dict: a dictionary of host keys linked to metric data for each
                metric name specified

        """
        info = {}
        self.log.info("Querying telemetry metric %s from %s", name, self.hosts)
        for host in self.hosts:
            data = self.dmg.telemetry_metrics_query(host=host, metrics=name)
            info[host] = {}
            if "response" in data:
                if "metric_sets" in data["response"]:
                    for entry in data["response"]["metric_sets"]:
                        info[host][entry["name"]] = {
                            "description": entry["description"],
                            "metrics": entry["metrics"]
                        }
        self.log.debug("INFO = %s", info)
        return info

    def get_container_metrics(self):
        """Get the container telemetry metrics.

        Returns:
            dict: dictionary of  of errors detected

        """
        names = [
            "engine_container_ops_open_total",
            "engine_container_ops_open_active",
            "engine_container_ops_close_total",
            "engine_container_ops_destroy_total",
        ]
        data = {}
        info = self.get_metrics(",".join(names))
        self.log.info("Container Telemetry Information")
        for host in info:
            data[host] = {key: 0 for key in names}
            for name in names:
                if name in info[host]:
                    description = info[host][name]["description"]
                    for metric in info[host][name]["metrics"]:
                        self.log.info(
                            "  %s (%s): %s (%s)",
                            description, name, metric["value"], host)
                        data[host][name] = metric["value"]
        return data

    def check_container_metrics(self, open_count=None, active_count=None,
                                close_count=None, destroy_count=None):
        """Verify the container telemetry metrics.

        Args:
            open_count (int, optional): Number of times cont_open has been
                called. Defaults to None.
            active_count (int, optional): Number of open container handles.
                Defaults to None.
            close_count (int, optional): Number of times cont_close has been
                called. Defaults to None.
            destroy_count (int, optional): Number of times cont_destroy has been
                called. Defaults to None.

        Returns:
            list: list of errors detected

        """
        errors = []
        names = [
            "engine_container_ops_open_total",
            "engine_container_ops_open_active",
            "engine_container_ops_close_total",
            "engine_container_ops_destroy_total",
        ]
        expected = {
            "engine_container_ops_open_total": open_count,
            "engine_container_ops_open_active": active_count,
            "engine_container_ops_close_total": close_count,
            "engine_container_ops_destroy_total": destroy_count,
        }
        data = self.get_container_metrics()
        for host in data:
            for name in names:
                if name in data[host]:
                    if (expected[name] is not None and
                            expected[name] != data[host][name]):
                        errors.append(
                            "{} mismatch on {}: expected={}; actual={}".format(
                                name, host, expected[name], data[host][name]))
                else:
                    errors.append("No {} data for {}".format(name, host))
        return errors
