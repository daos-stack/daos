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

    def check_container_metrics(self, open=None, active=None, close=None,
                                destroy=None):
        """Verify the container telemetry metrics.

        Args:
            open (int, optional): Number of times cont_open has been called.
                Defaults to None.
            active (int, optional): Number of open container handles. Defaults
                to None.
            close (int, optional): Number of times cont_close has been called.
                Defaults to None.
            destroy (int, optional): Number of times cont_destroy has been
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
            "engine_container_ops_open_total": open,
            "engine_container_ops_open_active": active,
            "engine_container_ops_close_total": close,
            "engine_container_ops_destroy_total": destroy,
        }
        info = self.get_metrics(",".join(names))
        self.log.info("Container Telemetry Information")
        for host in info:
            for name in names:
                if name in info[host]:
                    description = info[host][name]["description"]
                    for metric in info[host][name]["metrics"]:
                        value = metric["value"]
                        self.log.info(
                            "  %s (%s): %s (%s)",
                            description, name, metric["value"], host)
                        if (expected[name] is not None and
                                expected[name] != metric["value"]):
                            errors.append(
                                "{} mismatch on {} - {}: {} != {}".format(
                                    description, host, metric["label"],
                                    metric["value"], open))
                else:
                    errors.append("No {} data for {}".format(name, host))

        return errors
