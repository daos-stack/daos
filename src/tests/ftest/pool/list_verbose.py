#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase


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

    def test_fields_basic(self):
        """Verify all fields of dmg pool list --verbose except used and
        imbalance.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=pool,list_verbose,list_verbose_basic
        """
        # Create a pool with a given SCM and NVMe size.
        pool_namespace = "/run/pool_basic/*"
        self.add_pool(namespace=pool_namespace)

        expected_scm_size = self.params.get("scm_size", pool_namespace)
        expected_nvme_size = self.params.get("nvme_size", pool_namespace)

        # Prepare expected data.
        expected = {}
        expected[self.pool.uuid.lower()] = {
            "label": self.pool.label.value,
            "svc_reps": self.pool.svc_ranks,
            "scm_size": expected_scm_size,
            "scm_imbalance": 0,
            "nvme_size": expected_nvme_size,
            "nvme_imbalance": 0,
            "targets_total": self.params.get(
                "targets", "/run/server_config/servers/*"),
            "targets_disabled": 0
        }

        # Get the actual data.
        actual = {}
        output = self.pool.dmg.get_pool_list_all(verbose=True)

        actual_scm_size = -1
        actual_scm_free = -1
        actual_scm_imbalance = -1
        actual_nvme_size = -1
        actual_nvme_free = -1
        actual_nvme_imbalance = -1
        for usage in output[0]["usage"]:
            if usage["tier_name"] == "SCM":
                actual_scm_size = usage["size"]
                actual_scm_free = usage["free"]
                actual_scm_imbalance = usage["imbalance"]
            elif usage["tier_name"] == "NVME":
                actual_nvme_size = usage["size"]
                actual_nvme_free = usage["free"]
                actual_nvme_imbalance = usage["imbalance"]

        actual[output[0]["uuid"]] = {
            "label": output[0]["label"],
            "svc_reps": output[0]["svc_reps"],
            "scm_size": actual_scm_size,
            "scm_imbalance": actual_scm_imbalance,
            "nvme_size": actual_nvme_size,
            "nvme_imbalance": actual_nvme_imbalance,
            "targets_total": output[0]["targets_total"],
            "targets_disabled": output[0]["targets_disabled"]
        }

        # Compare expected and actual data.
        self.assertDictEqual(expected, actual)

        # Verify the free space. Take the initial overhead data into account.
        expected_scm_free_min = expected_scm_size * 0.9
        expected_nvme_free_min = expected_nvme_size * 0.6

        errors = []
        if expected_scm_free_min > actual_scm_free:
            errors.append(
                "SCM free too small! Expected at least {}; Actual {}".format(
                    expected_scm_free_min, actual_scm_free))

        if expected_nvme_free_min > actual_nvme_free:
            errors.append(
                "NVMe free too small! Expected at least {}; Actual {}".format(
                    expected_nvme_free_min, actual_nvme_free))

        if errors:
            self.fail("\n----- Errors detected! -----\n{}".format(
                "\n".join(errors)))

        self.destroy_containers(containers=self.container)
        self.destroy_pools(pools=self.pool)

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
        output = self.pool.dmg.get_pool_list_all(verbose=True)
        free_before = -1
        for usage in output[0]["usage"]:
            if usage["tier_name"] == storage:
                free_before = usage["free"]
                break

        # 3, 4. Create an S1 container and run 1GB IOR with S1.
        self.run_ior_with_pool()

        # 5, 6. Verify the free and imbalance.
        output = self.pool.dmg.get_pool_list_all(verbose=True)
        free_after = 0
        imbalance = 0
        for usage in output[0]["usage"]:
            if usage["tier_name"] == storage:
                free_after = usage["free"]
                imbalance = usage["imbalance"]
                break

        errors = []
        ior_write_size = self.params.get("ior_write_size", "/run/*")
        if free_after > free_before - ior_write_size:
            errors.append(
                "{} free is bigger than expected! Before {}; After {}".format(
                    storage, free_before, free_after))

        exp_imbalance = self.params.get(storage.lower(), "/run/exp_imbalance/*")
        # Imbalance can be 10 or 11.
        if imbalance <= exp_imbalance - 2 or imbalance >= exp_imbalance + 3:
            errors.append(
                "Unexpected {} imbalance! Expected {}; Actual {}".format(
                    storage, exp_imbalance, imbalance))

        # 7. Destroy the container and the pool.
        self.destroy_containers(containers=self.container)
        self.destroy_pools(pools=self.pool)

        return errors

    def test_used_imbalance(self):
        """Verify NVMe/SCM Used and Imbalance fields.

        1. Create a pool with --nvme-size=80GB.
        2. Store the NVMe Used.
        3. Create a POSIX container with --oclass=S1.
        4. Run IOR with oclass and dir_oclass = S1 to write a 1GB file.
        5. Verify the NVMe Free decreased by 1GB.
        6. Verify NVMe imbalance is 10%.
        7. Destroy the container and the pool.
        8. Repeat above steps with SCM-only pool.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=pool,list_verbose,list_verbose_imbalance
        """
        errors = []
        errors.extend(self.verify_used_imbalance("NVME"))
        errors.extend(self.verify_used_imbalance("SCM"))

        if errors:
            self.fail("\n----- Errors detected! -----\n{}".format(
                "\n".join(errors)))
