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
        :avocado: tags=hw,medium
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
        :avocado: tags=hw,medium
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
        pool_2_started = False
        for count in range(8):
            try:
                dmg_command.check_start(pool=pool_2.identifier)
                self.log.info("dmg check start pool_2 worked. - %d", count)
                pool_2_started = True
                break
            except CommandFailure as command_failure:
                # Starting back to back may cause Operation already performed error. In
                # that case, repeat. When the first pool is fixed, the second start should
                # work.
                self.log.info(
                    "dmg check start pool_2 failed. - %d; %s", count, command_failure)
            time.sleep(5)
        self.assertTrue(pool_2_started, "dmg check start pool_2 failed after 40 sec!")

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

    def test_two_pools_healthy(self):
        """Test to pass in two pool labels where one is healthy pool.

        1. Create three pools and one container.
        2. Inject container bad label into one of them.
        3. Enable checker and set policy to --all-interactive.
        4. Call dmg check start with two different healthy pool labels.
        5. Call dmg check start with two same healthy pool labels.
        6. Call dmg check start with healthy pool and corrupted pool.
        7. Repair with option 2 (original container label) and wait for checker to finish.
        8. Call dmg check start with healthy pool and invalid label.
        9. Disable checker and verify that the fault is actually fixed.

        Jira ID: DAOS-17858

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov
        :avocado: tags=DMGCheckStartCornerCaseTest,test_two_pools_healthy
        """
        # 1. Create three pools and one container.
        self.log_step("Create three pools and one container.")
        pool_1 = self.get_pool(connect=False)
        pool_2 = self.get_pool(connect=False)
        pool_3 = self.get_pool(connect=False)
        container = self.get_container(pool=pool_3)

        # 2. Inject container bad label into one of them.
        self.log_step("Inject container bad label into one of them.")
        daos_command = self.get_daos_command()
        daos_command.faults_container(
            pool=pool_3.identifier, cont=container.identifier,
            location="DAOS_CHK_CONT_BAD_LABEL")

        # 3. Enable checker and set policy to --all-interactive.
        self.log_step("Enable checker and set policy to --all-interactive.")
        dmg_command = self.get_dmg_command()
        dmg_command.check_enable()
        dmg_command.check_set_policy(all_interactive=True)

        # 4. Call dmg check start with two different healthy pool labels.
        self.log_step("Call dmg check start with two different healthy pool labels.")
        healthy_diff = pool_1.identifier + " " + pool_2.identifier
        try:
            dmg_command.check_start(pool=healthy_diff)
            msg = ("dmg check start with two different healthy pool labels worked as "
                   "expected.")
            self.log.info(msg)
        except CommandFailure as command_failure:
            msg = (f"dmg check start with two different healthy pool labels failed! "
                   f"{command_failure}")
            self.fail(msg)
        # Need to stop before starting again.
        dmg_command.check_stop()

        # 5. Call dmg check start with two same healthy pool labels.
        self.log_step("Call dmg check start with two same healthy pool labels.")
        healthy_same = pool_1.identifier + " " + pool_1.identifier
        try:
            dmg_command.check_start(pool=healthy_same)
            msg = ("dmg check start with two same healthy pool labels worked as "
                   "expected.")
            self.log.info(msg)
        except CommandFailure as command_failure:
            msg = (f"dmg check start with two same healthy pool labels failed! "
                   f"{command_failure}")
            self.fail(msg)
        dmg_command.check_stop()

        # 6. Call dmg check start with healthy pool and corrupted pool.
        self.log_step("Call dmg check start with healthy pool and corrupted pool.")
        healthy_corrupted = pool_1.identifier + " " + pool_3.identifier
        dmg_command.check_start(pool=healthy_corrupted)

        # 7. Repair with option 2 and wait for checker to finish.
        self.log_step("Repair with option 2 and wait for checker to finish.")
        # Wait for the checker to detect the inconsistent container label.
        query_reports = None
        for _ in range(8):
            check_query_out = dmg_command.check_query(pool=pool_3.identifier)
            # Status becomes RUNNING immediately, but it may take a while to detect the
            # inconsistency. If detected, "reports" field is filled.
            if check_query_out["response"]["status"] == "RUNNING":
                query_reports = check_query_out["response"]["reports"]
                if query_reports:
                    break
            time.sleep(5)
        if not query_reports:
            self.fail("Checker didn't detect any inconsistency!")
        fault_msg = query_reports[0]["msg"]
        expected_fault = "inconsistent container label"
        if expected_fault not in fault_msg:
            self.fail(f"Checker didn't detect {expected_fault}! Fault msg = {fault_msg}")
        # Obtain the seq num (ID) to repair.
        seq = query_reports[0]["seq"]
        # Repair with action 2, which is to use the original container label.
        dmg_command.check_repair(seq_num=str(seq), action="2")
        wait_for_check_complete(dmg=dmg_command)
        dmg_command.check_stop()

        # 8. Call dmg check start with healthy pool and invalid label.
        self.log_step("Call dmg check start with healthy pool and invalid label.")
        healthy_invalid = pool_1.identifier + " TestPool0"
        try:
            dmg_command.check_start(pool=healthy_invalid)
            self.fail("dmg check start with healthy and invalid pool labels worked!")
        except CommandFailure as command_failure:
            exp_msg = "unable to find pool service"
            if exp_msg not in str(command_failure):
                self.fail(f"{exp_msg} is not in the error message!")

        # 9. Disable checker and verify that the fault is actually fixed.
        self.log_step("Disable checker and verify that the fault is actually fixed.")
        dmg_command.check_disable()
        expected_props = {"label": container.label.value}
        label_verified = container.verify_prop(expected_props=expected_props)
        self.assertTrue(label_verified, "Container label isn't fixed!")
