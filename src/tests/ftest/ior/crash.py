"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time

from dmg_utils import check_system_query_status
from general_utils import wait_for_result
from ior_test_base import IorTestBase


class IorCrash(IorTestBase):
    """Test class Description:
        Verify DAOS server does not need to be restarted when an application crashes.
    :avocado: recursive
    """
    def verify_cont_handles(self, expected_handles=1):
        """Verify number of container handles. If needed, perform multiple queries (with delay).

        Args:
            expected_handles (int): expected number of container handles. Defaults to 1.

        Returns:
            bool: whether expected matches actual
        """
        return wait_for_result(
            self.log, self.container.verify_query, timeout=10, delay=2,
            expected_response={'num_handles': expected_handles})

    def test_ior_crash(self):
        """Jira ID: DAOS-4332.
           Jira ID: DAOS-9946.

        Test Description:
            Verify DAOS server does not need to be restarted when an application crashes.

        Use Cases:
            Run IOR Write.
            Kill IOR process in the middle of Write.
            Verify DAOS engines did not crash.
            Run IOR Write, Read.
            Kill IOR process in the middle of Read.
            Verify DAOS engines did not crash.
            Run IOR Write, Read, CheckRead.
            Verify IOR completes successfully.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,ior,dfs
        :avocado: tags=IorCrash,test_ior_crash
        """
        dmg = self.get_dmg_command()
        job_manager = self.get_ior_job_manager_command()

        # Create pool and container
        self.pool = self.get_pool(connect=False)
        self.container = self.get_container(self.pool)
        self.ior_cmd.set_daos_params(self.pool, self.container.identifier)

        # Don't check subprocess status, since output is buffered and can't be read in real time
        self.ior_cmd.pattern = None

        # Start IOR and crash it in the middle of Write
        self.run_ior_with_pool(create_pool=False, create_cont=False, job_manager=job_manager)
        time.sleep(self.ior_cmd.sw_deadline.value / 2)
        self.stop_ior(job_manager)

        # Verify engines did not crash
        scan_info = dmg.system_query(verbose=True)
        if not check_system_query_status(scan_info):
            self.fail("One or more engines crashed")

        # Verify container handle opened by ior is closed (by daos_agent after ior crash).
        # Expect to find one open handle now (a handle opened for this check)
        self.assertTrue(self.verify_cont_handles(), "Error confirming container info nhandles")

        # Run IOR and crash it in the middle of Read.
        # Must wait for Write to complete first.
        # Assumes Write and Read performance are about the same.
        self.run_ior_with_pool(create_pool=False, create_cont=False, job_manager=job_manager)
        time.sleep(self.ior_cmd.sw_deadline.value * 1.5)
        self.stop_ior(job_manager)

        # Verify engines did not crash
        scan_info = dmg.system_query(verbose=True)
        if not check_system_query_status(scan_info):
            self.fail("One or more engines crashed")

        # Verify container handle opened by ior is closed (by daos_agent after ior crash).
        self.assertTrue(self.verify_cont_handles(), "Error confirming container info nhandles")

        # Run IOR and verify it completes successfully
        self.run_ior_with_pool(create_pool=False, create_cont=False, job_manager=job_manager)

        # Verify engines did not crash
        scan_info = dmg.system_query(verbose=True)
        if not check_system_query_status(scan_info):
            self.fail("One or more engines crashed")

        # Verify container handle opened by ior is closed (by ior before its graceful exit)
        # Give ior some time to get started and open the container!
        # And, expect 2 open handles, one for this container open/query, and another for ior itself
        self.assertTrue(self.verify_cont_handles(expected_handles=2),
                        "Error confirming container info nhandles")
