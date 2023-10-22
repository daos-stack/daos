"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from ClusterShell.NodeSet import NodeSet

from apricot import TestWithServers
from run_utils import run_remote


class RecoveryTestBase(TestWithServers):
    # pylint: disable=no-member
    """Recovery test cases.

    Test Class Description:
        Used for recovery tests.

    :avocado: recursive
    """

    def get_vos_file_path(self, pool):
        """Get the VOS file path.

        If there are multiple VOS files, returns the first file obtained by "ls".

        Args:
            pool (TestPool): Pool.

        Returns:
            str: VOS file path such as /mnt/daos0/<pool_uuid>/vos-0

        """
        hosts = NodeSet(self.hostlist_servers[0])
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        vos_path = os.path.join(scm_mount, pool.uuid.lower())
        command = " ".join(["sudo", "ls", vos_path])
        cmd_out = run_remote(log=self.log, hosts=hosts, command=command)

        # return vos_file
        for file in cmd_out.output[0].stdout:
            # Assume the VOS file has "vos" in the file name.
            if "vos" in file:
                self.log.info("vos_file: %s", file)
                return file

        self.fail("vos file wasn't found in {}/{}".format(scm_mount, pool.uuid.lower()))

        return None  # to appease pylint
