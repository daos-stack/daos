#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase


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

    def create_expected(self, pool, scm_free, nvme_free, scm_imbalance,
                        nvme_imbalance, targets_disabled=0, scm_size=None,
                        nvme_size=None):
        """Create expected dmg pool list output to compare against the actual.

        Args:
            pool (TestPool): Pool used to fill in the output.
            scm_free (int): SCM free from the actual since direct comparison
                isn't easy.
            nvme_free (int): NVMe free from the actual since direct comparison
                isn't easy.
            scm_imbalance (int): SCM imbalance to fill in the output.
            nvme_imbalance (int): NVMe imbalance to fill in the output.
            targets_disabled (int, optional): Targets total to fill in the
                output. Defaults to 0.
            scm_size (int, optional): SCM size to fill in the output. Defaults
                to None.
            nvme_size (int, optional): NVMe size to fill in the output. Defaults
                to None.

        Returns:
            dict: Expected in the same format of actual.

        """
        # Prepare to calculate the size.
        rank_count = len(pool.target_list.value)
        # self.log.debug(
        #     "## Rank count = {}".format(rank_count))

        if scm_size is None:
            scm_size = pool.scm_size.value * rank_count
        if nvme_size is None:
            nvme_size = pool.nvme_size.value * rank_count

        targets_total = self.server_managers[0].get_config_value(
            "targets") * rank_count
        # self.log.debug("## targets_total = {}".format(targets_total))

        return {
            "uuid": pool.uuid.lower(),
            "label": pool.label.value,
            "svc_reps": pool.svc_ranks,
            "targets_total": targets_total,
            "targets_disabled": targets_disabled,
            "query_error_msg": "",
            "query_status_msg": "",
            "state": "Ready",
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
    def get_scm_nvme_free_imbalance(pool_list_out):
        """Get SCM and NVMe free and imbalance.

        Args:
            pool_list_out (list): pool list output list of dictionaries to
                obtain the free values from.

        Returns:
            dict: {uuid: {"scm": val, "nvme": val, "scm_imbalance": val,
                "nvme_imbalance": val}, uuid:...}

        """
        output = {}

        for pool in pool_list_out:
            scm_size = -1
            scm_free = -1
            nvme_free = -1
            scm_imbalance = -1
            nvme_imbalance = -1
            for usage in pool["usage"]:
                if usage["tier_name"] == "SCM":
                    scm_size = usage["size"]
                    scm_free = usage["free"]
                    scm_imbalance = usage["imbalance"]
                elif usage["tier_name"] == "NVME":
                    nvme_free = usage["free"]
                    nvme_imbalance = usage["imbalance"]

            output[pool["uuid"]] = {
                "scm_size": scm_size,
                "scm_free": scm_free,
                "nvme": nvme_free,
                "scm_imbalance": scm_imbalance,
                "nvme_imbalance": nvme_imbalance
            }

        return output

    def verify_scm_size(self, actual, created, rank_count):
        """Verify SCM size using the threshold.

        SCM size in pool list is slightly higher than the created value. Verify
        that it's smaller than the threshold (target_count * (4K - 1)).

        Args:
            actual (int): SCM size from pool list verbose.
            created (int): SCM size used to create the pool.
            rank_count (int): Number of ranks that the pool is created on.
        """
        targets = self.params.get("targets", "/run/server_config/*/")
        self.log.info("rank_count = %d; targets = %d", rank_count, targets)

        total_targets = rank_count * targets
        threshold = total_targets * 3999
        diff = actual - created
        self.log.info(
            "actual = %d; created = %d; diff = %d", actual, created, diff)

        msg = "Round up amount is too big! Threshold = {}, Diff = {}".format(
            threshold, diff)
        self.assertTrue(diff < threshold, msg)

    def verify_pool_lists(self, targets_disabled, scm_size, nvme_size):
        """Call dmg pool list and verify.

        self.pool should be a list. The elements of the inputs should
        correspond to the pools in self.pool.

        Args:
            targets_disabled (list): List of targets disabled for pools.
            scm_size (list): List of SCM size for pools.
            nvme_size (list): List of NVMe size for pools.
        """
        expected_pools = []

        actual_pools = self.get_dmg_command().get_pool_list_all(verbose=True)

        # Get free and imbalance from actual so that we can use them in
        # expected.
        free_data = self.get_scm_nvme_free_imbalance(actual_pools)

        # Create expected_pools. Use data from actual and the parameters.
        for index, pool in enumerate(self.pool):
            pool_free_data = free_data[pool.uuid.lower()]

            # Verify scm_size using the threshold rather than the exact match.
            rank_count = len(pool.target_list.value)
            if scm_size[index] is None:
                created = pool.scm_size.value * rank_count
            else:
                created = scm_size[index]
            self.verify_scm_size(
                pool_free_data["scm_size"], created, rank_count)

            # Verify the output except free, imbalance, and scm_size; they're
            # passed in to create_expected() to bypass the validation.
            expected_pools.append(
                self.create_expected(
                    pool=pool, scm_free=pool_free_data["scm_free"],
                    nvme_free=pool_free_data["nvme"],
                    scm_imbalance=pool_free_data["scm_imbalance"],
                    nvme_imbalance=pool_free_data["nvme_imbalance"],
                    targets_disabled=targets_disabled[index],
                    scm_size=pool_free_data["scm_size"],
                    nvme_size=nvme_size[index]))

        # Sort pools by UUID.
        actual_pools.sort(key=lambda item: item.get("uuid"))
        expected_pools.sort(key=lambda item: item.get("uuid"))

        self.assertListEqual(expected_pools, actual_pools)

        # For convenience.
        return actual_pools

    @staticmethod
    def get_free_imbalance(pool_dict, storage):
        """Get free and imbalance from a pool dictionary.

        Args:
            pool_dict (dict): Pool info from list pool.
            storage (str): SCM or NVME.
        """
        free = 0
        imbalance = 0
        for usage in pool_dict["usage"]:
            if usage["tier_name"] == storage:
                free = usage["free"]
                imbalance = usage["imbalance"]
                break

        return (free, imbalance)

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
        :avocado: tags=pool
        :avocado: tags=list_verbose,list_verbose_basic
        """
        self.maxDiff = None
        self.pool = []

        # 1. Create first pool with a given SCM and NVMe size.
        self.pool.append(self.get_pool(namespace="/run/pool_basic_1/*"))

        # 2. Verify the fields of pool 1.
        targets_disabled = [0]
        scm_size = [None]
        nvme_size = [None]
        self.verify_pool_lists(
            targets_disabled=targets_disabled, scm_size=scm_size,
            nvme_size=nvme_size)

        # 3. Create second pool.
        self.pool.append(self.get_pool(namespace="/run/pool_basic_2/*"))

        # 4. Verify the fields for both pools.
        # Fill in the expected target and size and pass them into
        # verify_pool_lists.
        targets_disabled.append(0)
        scm_size.append(None)
        nvme_size.append(None)
        self.verify_pool_lists(
            targets_disabled=targets_disabled, scm_size=scm_size,
            nvme_size=nvme_size)

        # 5. Exclude target 7 in rank 1 of pool 1.
        self.pool[0].exclude(ranks=[1], tgt_idx="7")

        # Sizes are reduced by 1/8.
        reduced_scm_size = self.pool[0].scm_size.value * 0.875
        reduced_nvme_size = self.pool[0].nvme_size.value * 0.875

        # 6. Verify the fields for both pools with expected disabled and size.
        targets_disabled[0] = 1
        scm_size[0] = reduced_scm_size
        nvme_size[0] = reduced_nvme_size
        self.verify_pool_lists(
            targets_disabled=targets_disabled, scm_size=scm_size,
            nvme_size=nvme_size)

        # 7-11. Destroy and verify until the pools are gone.
        while self.pool:
            self.pool[-1].destroy()
            self.pool.pop()

            # Update the expected targets/scm lists too.
            targets_disabled.pop()
            scm_size.pop()
            nvme_size.pop()

            self.verify_pool_lists(
                targets_disabled=targets_disabled, scm_size=scm_size,
                nvme_size=nvme_size)

    def verify_used_imbalance(self, storage):
        """Verification steps for test_used_imbalance.

        Args:
            storage (str): NVME or SCM.

        Returns:
            list: Errors.

        """
        self.maxDiff = None

        # 1. Create a pool of 80GB.
        self.pool = []
        if storage == "NVME":
            self.pool.append(self.get_pool(namespace="/run/pool_both/*"))
            nvme_size = [None]
        else:
            self.pool.append(self.get_pool(namespace="/run/pool_scm_only/*"))
            nvme_size = [0]

        # 2. Verify the pool created.
        targets_disabled = [0]
        scm_size = [None]
        actual_pools_before = self.verify_pool_lists(
            targets_disabled=targets_disabled, scm_size=scm_size,
            nvme_size=nvme_size)

        # 3. Store free.
        free_before, _ = self.get_free_imbalance(
            actual_pools_before[0], storage)

        # 4, 5. Create an S1 container and run 1GB IOR with S1.
        self.pool = self.pool[0]
        cmd_result = self.run_ior_with_pool()
        metrics = self.ior_cmd.get_ior_metrics(cmd_result)
        ior_write_size = int(metrics[0][22])
        self.log.info("IOR metrics = %s", metrics)
        self.log.info("ior_write_size = %d", ior_write_size)
        self.pool = [self.pool]

        # 6. Verify all fields except free and imbalance. Free and imbalance are
        # obtained from actual.
        actual_pools_after = self.verify_pool_lists(
            targets_disabled=targets_disabled, scm_size=scm_size,
            nvme_size=nvme_size)

        # Obtain the new free and imblanace.
        free_after, imbalance_after = self.get_free_imbalance(
            actual_pools_after[0], storage)

        # 7, 8. Verify the free and imbalance.
        errors = []
        if free_after > free_before - ior_write_size:
            errors.append(
                "{} free is bigger than expected! Before {}; After {}".format(
                    storage, free_before, free_after))

        # Imbalance can be 10 or 11 (expecting 10% for S1 with 10GB/target pool)
        if imbalance_after not in [10, 11]:
            errors.append(
                "Unexpected {} imbalance! Expected 10 or 11; Actual {}".format(
                    storage, imbalance_after))

        # 9. Destroy the container and the pool.
        self.destroy_containers(containers=self.container)
        self.destroy_pools(pools=self.pool)

        return errors

    def test_used_imbalance(self):
        """Verify NVMe/SCM Used and Imbalance fields.

        1. Create a pool with --nvme-size=80GB.
        2. Verify the pool created.
        3. Store the NVMe Used.
        4. Create a POSIX container with --oclass=S1.
        5. Run IOR with oclass and dir_oclass = S1 to write a 1GB file.
        6. Verify no change in before and after except in free and imbalance.
        7. Verify the NVMe Free decreased by 1GB.
        8. Verify NVMe imbalance is 10%.
        9. Destroy the container and the pool.
        10. Repeat above steps with SCM-only pool.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=list_verbose,list_verbose_imbalance
        """
        errors = []
        self.log.debug("---------- NVME test ----------")
        errors.extend(self.verify_used_imbalance("NVME"))
        self.log.debug("---------- SCM test ----------")
        errors.extend(self.verify_used_imbalance("SCM"))

        if errors:
            self.fail("\n----- Errors detected! -----\n{}".format("\n".join(errors)))
