"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
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

        return None  # to appease pylint

    def wait_for_check_complete(self):
        """Repeatedly call dmg check query until status becomes COMPLETED.

        If the status doesn't become COMPLETED, fail the test.

        Returns:
            list: List of repair reports.

        """
        repair_reports = None
        for _ in range(8):
            check_query_out = self.get_dmg_command().check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                repair_reports = check_query_out["response"]["reports"]
                break
            time.sleep(5)

        if not repair_reports:
            self.fail("Checker didn't detect or repair any inconsistency!")

        return repair_reports
