#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase
from ior_utils import IorCommand


# pylint: disable=too-many-ancestors
class ListVerboseTest(IorTestBase):
    """DAOS-8267: Test class for dmg pool list --verbose tests.

    Verify all fields of dmg pool list --verbose; Label, UUID, SvcReps, SCM
    Size, SCM Used, SCM Imbalance, NVME Size, NVME Used, NVME Imbalance, and
    Disabled.

    Control plane test and not the underlying algorithm test. Assume that a
    single non-zero output verifies all other non-zero output. e.g., if we can
    verify that 10 appears in imbalance as expected, we don't need to verify any
    of other values in imbalance.

    :avocado: recursive
    """

    def create_expected(self, pool, scm_free, nvme_free, targets_total=None,
                        targets_disabled=0, scm_size=None, scm_imbalance=0,
                        nvme_size=None, nvme_imbalance=0):
        """Create expected dmg pool list output to compare against the actual.

        Args:
            pool (TestPool): Pool used to fill in the output.
            scm_free (int): SCM free from the actual since direct comparison
                isn't easy.
            nvme_free (int): NVMe free from the actual since direct comparison
                isn't easy.
            targets_total (int, optional): Targets total to fill in the output.
                Defaults to None.
            targets_disabled (int, optional): Targets total to fill in the
                output. Defaults to None.
            scm_size (int, optional): SCM size to fill in the output. Defaults
                to None.
            scm_imbalance (int, optional): SCM imbalance to fill in the output.
                Defaults to 0.
            nvme_size (int, optional): NVMe size to fill in the output. Defaults
                to None.
            nvme_imbalance (int, optional): NVMe imbalance to fill in the
                output. Defaults to 0.

        Returns:
            dict: Expected in the same format of actual.

        """
        if targets_total is None:
            targets_total = self.server_managers[0].get_config_value("targets")
        if scm_size is None:
            scm_size = pool.scm_size.value
        if nvme_size is None:
            nvme_size = pool.nvme_size.value

        return {
            "uuid": pool.uuid.lower(),
            "label": pool.label.value,
            "svc_reps": pool.svc_ranks,
            "targets_total": targets_total,
            "targets_disabled": targets_disabled,
            "query_error_msg": "",
            "query_status_msg": "",
            "usage": [
            {
                "tier_name": "SCM",
                "size": scm_size,
                "free": scm_free,
                "imbalance": scm_imbalance
            },
            {
                "tier_name": "NVME",
                "size": nvme_size,
                "free": nvme_free,
                "imbalance": nvme_imbalance
            }
            ]
        }

    @staticmethod
    def get_scm_nvme_free(pool_list_out):
        """Get SCM and NVMe free values.

        Args:
            pool_list_out (dict): pool list output dictionary to obtain the free
                values from.

        Returns:
            tuple: SCM free and actual NVMe free values.
        """
        scm_free = -1
        nvme_free = -1
        for usage in pool_list_out["usage"]:
            if usage["tier_name"] == "SCM":
                scm_free = usage["free"]
            elif usage["tier_name"] == "NVME":
                nvme_free = usage["free"]

        return (scm_free, nvme_free)

    def verify_two_pools(self, targets_disabled_1=0, scm_size_1=None,
                         nvme_size_1=None):
        """Verify two pools by comparing expected and actual dictionaries.

        Args:
            targets_disabled_1 (int, optional): Targets disabled for pool 1.
                Defaults to 0.
            scm_size_1 (int, optional): SCM size for pool 1. Defaults to None.
            nvme_size_1 (int, optional): NVMe size for pool 1. Defaults to None.
        """
        actual_pools = self.pool[0].dmg.get_pool_list_all(verbose=True)

        # Get the dict of 1 and 2. Assume that the first two are what we want.
        if actual_pools[0]["uuid"] == self.pool[0].uuid.lower():
            actual_pool_1 = actual_pools[0]
            actual_pool_2 = actual_pools[1]
        else:
            actual_pool_1 = actual_pools[1]
            actual_pool_2 = actual_pools[0]

        # Get the free values and create expected for pool 1.
        actual_scm_free_1, actual_nvme_free_1 = self.get_scm_nvme_free(
            actual_pool_1)

        expected_pool_1 = self.create_expected(
            pool=self.pool[0], scm_free=actual_scm_free_1,
            nvme_free=actual_nvme_free_1, targets_disabled=targets_disabled_1,
            scm_size=scm_size_1, nvme_size=nvme_size_1)

        # Compare expected and actual data for pool 1.
        self.assertDictEqual(expected_pool_1, actual_pool_1)

        # Get the free values for pool 2.
        actual_scm_free_2, actual_nvme_free_2 = self.get_scm_nvme_free(
            actual_pool_2)

        # Pool 2 uses two ranks, so need to double the value in TestPool.
        scm_size_2 = self.pool[1].scm_size.value * 2
        nvme_size_2 = self.pool[1].nvme_size.value * 2

        expected_pool_2 = self.create_expected(
            pool=self.pool[1], scm_free=actual_scm_free_2,
            nvme_free=actual_nvme_free_2, scm_size=scm_size_2,
            nvme_size=nvme_size_2, targets_total=16)

        # Compare expected and actual data for pool 2.
        self.assertDictEqual(expected_pool_2, actual_pool_2)

    def test_fields_basic(self):
        """Verify all fields of dmg pool list --verbose except used and
        imbalance.

        1. Create a pool - pool 1.
        2. Verify the fields of pool 1.
        3. Create second pool - pool 2.
        4. Verify the fields for both pools.
        5. Exclude a target for pool 1.
        (dmg pool exclude TestLabel_1 --rank=1 --target-idx=7)
        6. Verify the fields for both pools with expected disabled and size.
        7. Destroy pool 2.
        8. Verify the fields for pool 1.
        9. Verify that there's only one pool in the list.
        10. Destroy pool 1.
        11. Verify that the list is empty.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool,list_verbose,list_verbose_basic
        """
        self.maxDiff = None
        self.pool = []

        # 1. Create first pool with a given SCM and NVMe size.
        self.pool.append(self.get_pool(namespace="/run/pool_basic_1/*"))

        # 2. Verify the fields of pool 1.
        actual = self.pool[0].dmg.get_pool_list_all(verbose=True)[0]
        actual_scm_free, actual_nvme_free = self.get_scm_nvme_free(actual)

        expected = self.create_expected(
            pool=self.pool[0], scm_free=actual_scm_free,
            nvme_free=actual_nvme_free)

        self.assertDictEqual(expected, actual)

        # 3. Create second pool.
        self.pool.append(self.get_pool(namespace="/run/pool_basic_2/*"))

        # 4. Verify the fields for both pools.
        self.verify_two_pools()

        # 5. Exclude target 7 in rank 1 of pool 1.
        self.pool[0].exclude(ranks=[1], tgt_idx="7")

        # Sizes are reduced by 1/8.
        scm_size_1 = self.pool[0].scm_size.value * 0.875
        nvme_size_1 = self.pool[0].nvme_size.value * 0.875

        # 6. Verify the fields for both pools with expected disabled and size.
        self.verify_two_pools(
            targets_disabled_1=1, scm_size_1=scm_size_1,
            nvme_size_1=nvme_size_1)

        # 7. Destroy pool 2.
        self.pool[1].destroy()

        # 8. Verify the fields for pool 1.
        actual = self.pool[0].dmg.get_pool_list_all(verbose=True)
        actual_scm_free, actual_nvme_free = self.get_scm_nvme_free(actual[0])

        expected = self.create_expected(
            pool=self.pool[0], scm_free=actual_scm_free,
            nvme_free=actual_nvme_free, targets_disabled=1, scm_size=scm_size_1,
            nvme_size=nvme_size_1)

        self.assertDictEqual(expected, actual[0])

        # 9. Verify that there's only one pool in the list.
        self.assertEqual(len(actual), 1)

        # 10. Destroy pool 1.
        self.pool[0].destroy()

        # 11. Verify that the list is empty.
        actual = self.pool[0].dmg.get_pool_list_all(verbose=True)
        self.assertEqual(len(actual), 0)

    def verify_used_imbalance(self, storage):
        """Verification steps for test_used_imbalance.

        Args:
            storage (str): NVME or SCM.

        Returns:
            list: Errors.

        """
        # 1. Create a pool with 80GB.
        if storage == "NVME":
            self.add_pool(namespace="/run/pool_both/*")
        else:
            self.add_pool(namespace="/run/pool_scm_only/*")

        # 2. Store free.
        before = self.pool.dmg.get_pool_list_all(verbose=True)[0]
        free_before = -1
        for usage in before["usage"]:
            if usage["tier_name"] == storage:
                free_before = usage["free"]

        # 3, 4. Create an S1 container and run 1GB IOR with S1.
        cmd_result = self.run_ior_with_pool()
        metrics = IorCommand.get_ior_metrics(cmd_result)
        ior_write_size = int(metrics[0][22])
        self.log.info("IOR metrics = %s", metrics)
        self.log.info("ior_write_size = %d", ior_write_size)

        # Obtain the new free and imblanace.
        after = self.pool.dmg.get_pool_list_all(verbose=True)[0]
        free_after = 0
        imbalance = 0
        for usage in after["usage"]:
            if usage["tier_name"] == storage:
                free_after = usage["free"]
                imbalance = usage["imbalance"]
                break

        # Copy free_before and imbalance = 0 to after so that we can
        # directly compare.
        scm_free_before, nvme_free_before = self.get_scm_nvme_free(before)
        for usage in after["usage"]:
            if usage["tier_name"] == "NVME":
                usage["free"] = nvme_free_before
                usage["imbalance"] = 0
            elif usage["tier_name"] == "SCM":
                usage["free"] = scm_free_before
                usage["imbalance"] = 0

        # 5. Compare before and after except free and imbalance.
        self.assertDictEqual(before, after)

        # 6, 7. Verify the free and imbalance.
        errors = []
        if free_after > free_before - ior_write_size:
            errors.append(
                "{} free is bigger than expected! Before {}; After {}".format(
                    storage, free_before, free_after))

        # Imbalance can be 10 or 11 (expecting 10% for S1 with 10GB/target pool)
        if imbalance not in [10, 11]:
            errors.append(
                "Unexpected {} imbalance! Expected 10 or 11; Actual {}".format(
                    storage, imbalance))

        # 8. Destroy the container and the pool.
        self.destroy_containers(containers=self.container)
        self.destroy_pools(pools=self.pool)

        return errors

    def test_used_imbalance(self):
        """Verify NVMe/SCM Used and Imbalance fields.

        1. Create a pool with --nvme-size=80GB.
        2. Store the NVMe Used.
        3. Create a POSIX container with --oclass=S1.
        4. Run IOR with oclass and dir_oclass = S1 to write a 1GB file.
        5. Verify no change in before and after except in free and imbalance.
        6. Verify the NVMe Free decreased by 1GB.
        7. Verify NVMe imbalance is 10%.
        8. Destroy the container and the pool.
        9. Repeat above steps with SCM-only pool.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool,list_verbose,list_verbose_imbalance
        """
        errors = []
        self.log.debug("---------- NVME test ----------")
        errors.extend(self.verify_used_imbalance("NVME"))
        self.log.debug("---------- SCM test ----------")
        errors.extend(self.verify_used_imbalance("SCM"))

        if errors:
            self.fail("\n----- Errors detected! -----\n{}".format(
                "\n".join(errors)))
