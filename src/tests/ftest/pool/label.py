#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from command_utils import CommandFailure
from avocado.core.exceptions import TestFail


class Label(TestWithServers):
    """Test create and destroy a pool with a lable.

    :avocado: recursive
    """

    def verify_input(self, label, failure_expected):
        """Pool create and destroy with given label.

        Args:
            label (str): Label.
            failure_expected (bool): Whether failure is expected from pool
                create or destroy.

        Returns:
            list: List of errors.

        """
        errors = []

        self.pool.append(self.get_pool(create=False))
        self.pool[-1].label.update(label)

        try:
            self.pool[-1].create()
            if failure_expected:
                error_message = "dmg pool create is expected to fail, " +\
                    "but worked! {}".format(label)
                errors.append(error_message)
        except TestFail:
            if not failure_expected:
                error_message = "dmg pool create with {} failed! ".format(label)
                self.log.info(error_message)
                errors.append(error_message)

        if not failure_expected:
            if not self.pool[-1].destroy():
                error_message = "dmg pool destroy with {} failed! ".format(
                    label)
                self.log.info(error_message)
                errors.append(error_message)

        return errors

    def check_errors(self, errors):
        """Check if there's any error in given list. If so, fail the test.

        Args:
            errors (list): List of errors.

        """
        if errors:
            self.fail("\n----- Errors detected! -----\n{}".format(
                "\n".join(errors)))

    def test_various_labels(self):
        """Test ID: DAOS-7942

        Test Description: Create and destroy pool with the following.
        1. UUID format string: 23ab123e-5296-4f95-be14-641de40b4d5a
        2. Long label - 126 chars. Should pass.
        3. Long label - 127 chars. Should pass.
        4. Long label - 128 chars. Should fail.

        :avocado: tags=all,full_regression
        :avocado: tags=small
        :avocado: tags=pool,create_various_labels
        """
        self.pool = []
        errors = []

        uuid_format = "23ab123e-5296-4f95-be14-641de40b4d5a"
        long_label_126 = "1234567890" * 12 + "123456"
        long_label_127 = "1234567890" * 12 + "1234567"
        long_label_128 = "1234567890" * 12 + "12345678"

        errors.extend(self.verify_input(uuid_format, True))
        errors.extend(self.verify_input(long_label_126, False))
        # DAOS-8183
        # errors.extend(self.verify_input(long_label_127, False))
        errors.extend(self.verify_input(long_label_128, True))

        self.check_errors(errors)

    def test_duplicate_create(self):
        """Test ID: DAOS-7942

        Test Description:
        1. Create a pool with the same label twice. Second create should fail.
        2. Destroy it.
        3. Create a pool with the same label again. It should work this time.

        Note that we can't use TestPool in this test because the create() does
        destroy, so the duplicate create would always work.

        :avocado: tags=all,full_regression
        :avocado: tags=small
        :avocado: tags=pool,duplicate_label_create
        """
        dmg = self.get_dmg_command()
        label = "TestLabel"
        errors = []

        dmg.pool_create(scm_size="1G", label=label)

        try:
            dmg.pool_create(scm_size="1G", label=label)
            error_msg = "dmg pool create with duplicate label worked!"
            errors.append(error_msg)
        except CommandFailure:
            self.log.info(
                "dmg pool create with duplicate label failed as expected")

        dmg.pool_destroy(pool=label)

        try:
            dmg.pool_create(scm_size="1G", label=label)
        except CommandFailure:
            error_msg = "dmg pool recreate with the same label failed!"
            errors.append(error_msg)

        self.check_errors(errors)

    def test_duplicate_destroy(self):
        """Test ID: DAOS-7942

        Test Description:
        1. Create a pool with a label.
        2. Destroy it with the label.
        3. Destroy it with the label again. The second destroy should fail.

        Note that we don't use TestPool in this test because destory() checks if
        self.pool is None, but we just want to call dmg without this pre-check.

        :avocado: tags=all,full_regression
        :avocado: tags=small
        :avocado: tags=pool,duplicate_label_destroy
        """
        dmg = self.get_dmg_command()
        label = "TestLabel"
        errors = []

        dmg.pool_create(scm_size="1G", label=label)

        dmg.pool_destroy(pool=label)

        try:
            dmg.pool_destroy(pool=label)
            error_message = "Duplicate destroy succeeded!"
            errors.append(error_message)
        except CommandFailure:
            self.log.info("Duplicate destroy failed as expected")

        self.check_errors(errors)
