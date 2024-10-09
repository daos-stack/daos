"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse


class DfuseContainerAttrs(TestWithServers):
    """Check if the dfuse attributes of a container are properly managed.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super().setUp()

    def test_dfuse_container_attrs(self):
        """Jira ID: DAOS-14698.

        Test Description:
            Create a container with DFuse attributes
            Mount a DFuse mount point
            Check the output of the DFuse log
        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=dfuse,container
        :avocado: tags=DfuseContainerAttrs,test_dfuse_container_attrs
        """

        self.log.info("Creating DAOS pool")
        pool = self.get_pool()

        self.log_step("Creating DAOS container with user attributes")
        container = self.get_container(pool, namespace="/run/container_01/*")

        self.log_step("Mounting DFuse mount point")
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        self.log_step("Checking DFuse log file")
        log_file = dfuse.get_log_file()
        attrs = self.params.get("attrs", "/run/container_01/*")
        for attr in [attr.split(':') for attr in attrs.split(",")]:
            match = None
            attr_re = re.compile(r"^.+\ssetting\s+'" + attr[0] + r"'\s+is\s+(\d+)\s+seconds$")
            for line in log_file:
                match = attr_re.match(line)
                if match:
                    self.assertEqual(
                        attr[1],
                        match.group(1),
                        f"Unexpected value for attribute {attr[0]}: "
                        f"want={attr[1]}, got={match.group(1)}")
                    break
            self.assertIsNotNone(match, f"Setting of attribute {attr[0]} not found")

        self.log_step("Test passed")
