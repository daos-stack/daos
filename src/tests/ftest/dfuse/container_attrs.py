"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from run_utils import run_remote


class DfuseContainerAttrs(TestWithServers):
    """Check if the dfuse attributes of a container are properly managed.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DfuseContainerAttrs object"""
        super().__init__(*args, **kwargs)
        self.dfuse_hosts = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super().setUp()

        # pylint: disable-next=fixme
        # FIXME DFuse is mounted and checked on the launcher node until DAOS-7164 will be fixed.  At
        # this time, it is not possible to create a DFuse sub-container and destroy it properly as
        # it has to be done on one of the client node holding the root DFuse mount point.
        self.dfuse_hosts = self.agent_managers[0].hosts

    def _check_attrs(self, dfuse, attrs=None, namespace=None):
        """Check if the DFuse attributes of a container are loaded

        Check in the log file of the dfuse instance if it contains the DFuse attributes of a given
        container.  It also checks the value of the attributes found.

        Args:
            dfuse (Dfuse): DFuse instance to check
            attrs (dict, optional): list of attributes to test
                Defaults to None
            namespace (str, optional): Namespace for TestContainer parameters in the test yaml file.
                Defaults to None
        """
        if attrs is None:
            attrs = {}

        if namespace is not None:
            for attr in self.params.get("attrs", namespace).split(","):
                key, value = attr.split(':')
                if key not in attrs:
                    attrs[key] = value

        log_file = os.linesep.join(dfuse.get_log_file_data().output[0].stdout)
        for name, value in attrs.items():
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

    def test_dfuse_container_create_attrs(self):
        """Jira ID: DAOS-14698.

        Test Description:
            Create a container with DFuse attributes
            Mount a DFuse mount point
            Check the output of the DFuse log
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,container
        :avocado: tags=DfuseContainerAttrs,test_dfuse_container_create_attrs
        """
        self.log.info("Creating DAOS pool")
        pool = self.get_pool()

        self.log_step("Creating DAOS container with Dfuse attributes")
        container = self.get_container(pool, namespace="/run/container_01/*")

        self.log_step("Mounting DFuse mount point")
        dfuse = get_dfuse(self, self.dfuse_hosts)
        dfuse.env["D_LOG_FLUSH"] = "INFO"
        start_dfuse(self, dfuse, pool, container)

        self.log_step("Checking DFuse log file")
        self._check_attrs(dfuse, namespace="/run/container_01/*")

        self.log_step("Test passed")

    def test_dfuse_subcontainer_create_attrs(self):
        """Jira ID: DAOS-14698.

        Test Description:
            Create a container
            Mount a DFuse mount point
            Create a sub-container with DFuse attributes
            Check the output of the DFuse log

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,container
        :avocado: tags=DfuseContainerAttrs,test_dfuse_subcontainer_create_attrs
        """
        self.log.info("Creating DAOS pool")
        pool = self.get_pool()

        self.log_step("Creating DAOS container")
        container = self.get_container(pool, namespace="/run/container_02/*")

        self.log_step("Mounting DFuse mount point")
        dfuse = get_dfuse(self, self.dfuse_hosts)
        dfuse.env["D_LOG_FLUSH"] = "INFO"
        start_dfuse(self, dfuse, pool, container)

        self.log_step("Creating DAOS subcontainer with DFuse attributes")
        sub_dir = os.path.join(dfuse.mount_dir.value, "foo")
        self.get_container(pool, namespace="/run/container_03/*", path=sub_dir)

        self.log_step("Checking DFuse log file")
        self._check_attrs(dfuse, namespace="/run/container_03/*")

        self.log_step("Test passed")

    def test_dfuse_subcontainer_set_attrs(self):
        """Jira ID: DAOS-14698.

        Test Description:
            Create a container
            Mount a DFuse mount point
            Create a sub-container
            Set DFuse attributes to the sub-container
            Evict the DFuse sub-container
            Stat the DFuse sub-container mount point
            Check the output of the DFuse log

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,container
        :avocado: tags=DfuseContainerAttrs,test_dfuse_subcontainer_set_attrs
        """
        self.log.info("Creating DAOS pool")
        pool = self.get_pool()

        self.log_step("Creating DAOS container")
        container = self.get_container(pool, namespace="/run/container_04/*")

        self.log_step("Mounting DFuse mount point")
        dfuse = get_dfuse(self, self.dfuse_hosts)
        dfuse.env["D_LOG_FLUSH"] = "INFO"
        start_dfuse(self, dfuse, pool, container)

        self.log_step("Creating DAOS sub-container")
        sub_dir = os.path.join(dfuse.mount_dir.value, "bar")
        sub_container = self.get_container(pool, namespace="/run/container_05/*", path=sub_dir)

        self.log_step("Setting DFuse attributes to the DAOS sub-container")
        attrs = {
            "dfuse-attr-time": "153",
            "dfuse-dentry-time": "407"}
        sub_container.daos.container_set_attr(pool.identifier, sub_container.identifier, attrs)

        self.log_step("Evicting the DAOS sub-container")
        sub_container.daos.filesystem_evict(sub_dir)

        self.log_step("Running stat on the DFuse sub-container mount point")
        result = run_remote(self.log, self.dfuse_hosts, f"stat {sub_dir}")
        if not result.passed:
            self.fail(f"stat on {sub_dir} can not be run on {result.failed_hosts}")

        self.log_step("Checking DFuse log file")
        self._check_attrs(dfuse, attrs=attrs)

        self.log_step("Test passed")
