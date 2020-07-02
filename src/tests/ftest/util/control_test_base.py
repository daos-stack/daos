#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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

from avocado import fail_on
from apricot import TestWithServers
from general_utils import get_host_data
from command_utils import CommandFailure


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

    def get_superblock_info(self, sp_file, sp_value):
        """Get the superblock information for each host.

        Args:
            sp_file (str): scm mount path.
            sp_value (str): superblock file value to extract.
                i.e. version, uuid, system, rank, validrank, ms

        Returns:
            dict: a dictionary of data values for each NodeSet key

        """
        pattern = r"^{}:\s+([_a-z0-9-]+).*".format(sp_value)
        cmd = r"cat {} | sed -En 's/{}/\1 /gp'".format(sp_file, pattern)
        text = "superblock"
        error = "Error obtaining superblock info: {}".format(sp_value)

        return get_host_data(self.dmg.hostlist, cmd, text, error, 20)

    def cleanup_output(self, output):
        """Cleanup output that is of this form:
            [(host, "", ""), ("", "data1", "data2"), ("", "data1", "data2")]

        Args:
            output (list): output to be parsed.

        Returns:
            dict: integrated dictionary containing information for each host.

        """
        host = None
        info = {}
        for item in output:
            if item[0]:
                host = item[0]
                continue
            if host not in info:
                info[host] = []
            info[host].append(item[1:])
        return info

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
            info = self.cleanup_output(
                self.get_dmg_output("storage_query_list_devices", **kwargs))
        return info

    def get_pool_info(self, uuid=None, rank=None, verbose=False):
        """Query pool information.

        Args:
            uuid (str): Device UUID to query. Defaults to None.
            rank (int, optional): Limit response to devices on this rank.
                Defaults to None.
            verbose (bool, optional): create verbose output. Defaults to False.
        """
        kwargs = {"uuid": uuid, "rank": rank, "verbose": verbose}
        return self.cleanup_output(
            self.get_dmg_output("storage_query_list_pools", **kwargs))
