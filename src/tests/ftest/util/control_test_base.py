"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet


class ControlTestBase(TestWithServers):
    """Defines common methods for control tests.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ControlTestBase object."""
        super().__init__(*args, **kwargs)
        self.dmg = None

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.dmg = self.get_dmg_command()

    def verify_dmg_storage_scan(self, verify_method):
        """Call dmg storage scan and run the given method with the output.

        Args:
            verify_method (method): Method that uses the generated output. Must
                return list of errors.
        """
        errors = []

        for manager in self.server_managers:
            data = manager.dmg.storage_scan(verbose=True)

            if manager.dmg.result.exit_status == 0:
                for struct_hash in data["response"]["HostStorage"]:
                    hash_dict = data["response"]["HostStorage"][struct_hash]
                    hosts = NodeSet(hash_dict["hosts"].split(":")[0])
                    if hosts in manager.hosts:
                        errors.extend(verify_method(hash_dict["storage"]))
            else:
                errors.append("dmg storage scan failed!")

        if errors:
            self.fail("\n--- Errors found! ---\n{}".format("\n".join(errors)))
