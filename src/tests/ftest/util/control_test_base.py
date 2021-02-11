#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from avocado import fail_on
from collections import defaultdict
from apricot import TestWithServers
from general_utils import get_host_data
from command_utils import CommandFailure


def cleanup_output(output):
    """Cleanup output that is of this form:
        [(host, "", ""), ("", "data1", "data2"), ("", "data1", "data2")]

    Args:
        output (list): output to be parsed.

    Returns:
        dict: integrated dictionary containing information for each host.

    """
    host = None
    info = defaultdict(list)
    for item in output:
        if item[0]:
            host = item[0]
            continue
        info[host].append(item[1:])
    return info


class ControlTestBase(TestWithServers):
    # pylint: disable=too-few-public-methods,too-many-ancestors
    """Defines common methods for control tests.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ControlTestBase object."""
        super(ControlTestBase, self).__init__(*args, **kwargs)
        self.dmg = None

    def setUp(self):
        """Set up each test case."""
        super(ControlTestBase, self).setUp()
        self.dmg = self.get_dmg_command()

    @fail_on(CommandFailure)
    def get_dmg_output(self, method_name, regex_method=None, **kwargs):
        """Run the dmg command."""
        return self.dmg.get_output(method_name, regex_method, **kwargs)

    def get_device_info(self, rank=None, health=None):
        """Query storage device information.

        Args:
            rank (int, optional): Limit response to devices on this rank.
                Defaults to None.
            health (bool, optional): Include device health in response.
                Defaults to false.

        Returns:
            list: device info containing lists with queried device information.

        """
        info = None
        kwargs = {"rank": rank, "health": health}
        if health:
            info = self.get_dmg_output(
                "storage_query_list_devices",
                "storage_query_device_health",
                **kwargs)
        else:
            info = cleanup_output(
                self.get_dmg_output("storage_query_list_devices", **kwargs))
        return info

    def get_pool_info(self, uuid=None, rank=None, verbose=False):
        """Query pool information.

        Args:
            uuid (str): Device UUID to query. Defaults to None.
            rank (int, optional): Limit response to devices on this rank.
                Defaults to None.
            verbose (bool, optional): create verbose output. Defaults to False.

        Returns:
            dict: Dictionary containing pool information for each host.

        """
        kwargs = {"uuid": uuid, "rank": rank, "verbose": verbose}
        return cleanup_output(
            self.get_dmg_output("storage_query_list_pools", **kwargs))
