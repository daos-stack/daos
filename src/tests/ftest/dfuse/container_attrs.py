"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
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

    def _check_attrs(self, dfuse, container_name):
        """Check if the DFuse attributes of a container are loaded

        Check in the log file of the dfuse instance if it contains the DFuse attributes of a given
        container.  It also checks the value of the attributes found.

        Args:
            dfuse (Dfuse): DFuse instance to check
            container_name (str): Name of the container
        """
        log_file = os.linesep.join(dfuse.get_log_file_data().output[0].stdout)
        attrs = self.params.get("attrs", f"/run/{container_name}/*")
        for name, value in [attr.split(':') for attr in attrs.split(",")]:
            match = re.findall(
                fr"^.+\ssetting\s+'{name}'\s+is\s+(\d+)\s+seconds$",
                log_file,
                re.MULTILINE)
            self.assertEqual(
                len(match),
                1,
                f"Unexpected number setting(s) of attribute {name}: want=1, got={len(match)}")
            self.assertEqual(
                value,
                match[0],
                f"Unexpected value for attribute {name}: want={value}, got={match[0]}")

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

        self.log_step("Creating DAOS container with Dfuse attributes")
        container = self.get_container(pool, namespace="/run/container_01/*")

        self.log_step("Mounting DFuse mount point")
        dfuse = get_dfuse(self, self.hostlist_clients)
        dfuse.env["D_LOG_FLUSH"] = "INFO"
        start_dfuse(self, dfuse, pool, container)

        self.log_step("Checking DFuse log file")
        self._check_attrs(dfuse, "container_01")

        self.log_step("Test passed")

    def test_dfuse_subcontainer_attrs(self):
        """Jira ID: DAOS-14698.

        Test Description:
            Create a container
            Mount a DFuse mount point
            Create a sub-container with DFuse attributes
            Check the output of the DFuse log
        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=dfuse,container
        :avocado: tags=DfuseContainerAttrs,test_dfuse_subcontainer_attrs
        """
        self.log.info("Creating DAOS pool")
        pool = self.get_pool()

        self.log_step("Creating DAOS container")
        container = self.get_container(pool, namespace="/run/container_02/*")

        self.log_step("Mounting DFuse mount point")
        dfuse = get_dfuse(self, self.hostlist_clients)
        dfuse.env["D_LOG_FLUSH"] = "INFO"
        start_dfuse(self, dfuse, pool, container)

        self.log_step("Creating DAOS subcontainer with DFuse attributes")
        sub_dir = os.path.join(dfuse.mount_dir.value, "foo")
        container = self.get_container(pool, namespace="/run/container_03/*", path=sub_dir)

        self.log_step("Checking DFuse log file")
        self._check_attrs(dfuse, "container_03")

        self.log_step("Test passed")
