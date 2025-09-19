"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from exception_utils import CommandFailure
from recovery_utils import wait_for_check_complete


class DMGCheckStartCornerCaseTest(TestWithServers):
    """Test dmg check start corner cases.

    :avocado: recursive
    """

    def test_start_single_pool(self):
        """Test dmg check start corner cases with single healthy pool.

        1. Create a pool and enable checker.
        2. Start with the pool label. It should not detect any fault.
        3. Start with non-existing pool label. Verify error message.

        Jira ID: DAOS-17820

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=recovery,cat_recov
        :avocado: tags=DMGCheckStartCornerCaseTest,test_start_single_pool
        """
        # 1. Create a pool and enable checker.
        self.log_step("Create a pool and enable checker.")
        pool = self.get_pool(connect=False)
        dmg_command = self.get_dmg_command()
        dmg_command.check_enable()

        # 2. Start with the pool label. It should not detect any fault.
        self.log_step("Start with the pool label. It should not detect any fault.")
        dmg_command.check_start(pool=pool.identifier)
        query_reports = None
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                query_reports = check_query_out["response"]["reports"]
                break
            time.sleep(5)
        if query_reports:
            msg = (f"dmg check start with healthy pool detected inconsistency! "
                   f"{query_reports}")
            self.fail(msg)

        # 3. Start with non-existing pool label. Verify error message.
        self.log_step("Start with non-existing pool label. Verify error message.")
        try:
            dmg_command.check_start(pool="invalid_label")
            self.fail("dmg check start invalid_label worked!")
        except CommandFailure as command_failure:
            if "unable to find pool service" not in str(command_failure):
                msg = (f"dmg check start invalid_label didn't return expected message! "
                       f"{command_failure}")
                self.fail(msg)
            self.log.info("dmg check start invalid_label failed as expected.")

        dmg_command.check_disable()

    def test_start_back_to_back(self):
        """Test dmg check start <pool_1> and <pool_2> back to back.

        1. Create two pools and a container.
        2. Inject fault on both containers.
        3. Enable checker.
        4. Start with the first pool.
        5. Immediately after starting the first pool, start the second pool. This may
        result in Operation already performed error. In that case, repeat. When the first
        pool is fixed, the second start should work.
        6. Query checker and verify that they’re fixed.
        7. Disable checker and start system.
        8. Verify that the faults are actually fixed.

        Jira ID: DAOS-17860

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=recovery,cat_recov
        :avocado: tags=DMGCheckStartCornerCaseTest,test_start_back_to_back
        """
        # 1. Create two pools and a container.
        self.log_step("Create two pools and a container.")
        pool_1 = self.get_pool(connect=False)
        pool_2 = self.get_pool(connect=False)
        container_1 = self.get_container(pool=pool_1)
        container_2 = self.get_container(pool=pool_2)

        # 2. Inject fault on both containers.
        self.log_step("Inject fault on both containers.")
        daos_command = self.get_daos_command()
        daos_command.faults_container(
            pool=pool_1.identifier, cont=container_1.identifier,
            location="DAOS_CHK_CONT_ORPHAN")
        daos_command.faults_container(
            pool=pool_2.identifier, cont=container_2.identifier,
            location="DAOS_CHK_CONT_ORPHAN")

        # 3. Enable checker.
        self.log_step("Enable checker.")
        dmg_command = self.get_dmg_command()
        dmg_command.check_enable()

        # 4. Start with the first pool.
        self.log_step("Start with the first pool.")
        dmg_command.check_start(pool=pool_1.identifier)

        # 5. Immediately after starting the first pool, start the second pool.
        self.log_step("Immediately after starting the first pool, start the second pool.")
        for count in range(8):
            try:
                dmg_command.check_start(pool=pool_2.identifier)
                self.log.info("dmg check start pool_2 worked. - %d", count)
                break
            except CommandFailure as command_failure:
                # Starting back to back may cause Operation already performed error. In
                # that case, repeat. When the first pool is fixed, the second start should
                # work.
                self.log.info(
                    "dmg check start pool_2 failed. - %d; %s", count, command_failure)
            time.sleep(5)

        # 6. Query checker and verify that they’re fixed.
        self.log_step("Query checker and verify that they’re fixed.")
        wait_for_check_complete(dmg=dmg_command)

        # 7. Disable checker and start system.
        self.log_step("Disable checker and start system.")
        dmg_command.check_disable()

        # 8. Verify that the faults are actually fixed.
        self.log_step("Verify that the faults are actually fixed.")
        # In this case, check that the containers were removed.
        container_list_out_1 = daos_command.pool_list_containers(pool=pool_1.identifier)
        container_list_out_2 = daos_command.pool_list_containers(pool=pool_2.identifier)
        container_list_1 = container_list_out_1["response"]
        container_list_2 = container_list_out_2["response"]
        if container_list_1:
            self.fail(f"Pool 1 container wasn't removed! {container_list_1}")
        if container_list_2:
            self.fail(f"Pool 2 container wasn't removed! {container_list_2}")

        # Containers were removed by the checker.
        container_1.skip_cleanup()
        container_2.skip_cleanup()
