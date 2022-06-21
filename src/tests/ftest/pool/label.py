#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import string

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from general_utils import report_errors, get_random_string
from exception_utils import CommandFailure


class Label(TestWithServers):
    """Test create and destroy a pool with a label.

    :avocado: recursive
    """

    def verify_destroy(self, pool, failure_expected, use_dmg=False):
        """Verify pool destroy works/not work as expected.

        Args:
            pool (TestPool): Pool to destroy.
            failure_expected (bool): Whether failure is expected from pool
                destroy.
            use_dmg (bool): Whether to use dmg object. Defaults to False.

        Returns:
            list: List of errors.

        """
        errors = []

        try:
            if use_dmg:
                pool.dmg.pool_destroy(pool=pool.label.value, force=1)
            else:
                pool.destroy()
            if failure_expected:
                error_message = "dmg pool destroy is expected to fail, " +\
                    "but worked!"
                errors.append(error_message)
        except (TestFail, CommandFailure):
            if not failure_expected:
                error_message = "dmg pool destroy failed! "
                self.log.info(error_message)
                errors.append(error_message)

        return errors

    def verify_create(self, label, failure_expected, expected_error=None):
        """Verify pool create with given label works/not work as expected.

        Args:
            label (str): Label.
            failure_expected (bool): Whether failure is expected from pool
                create.
            expected_error (str): Expected error message. Defaults to None.

        Returns:
            list: List of errors.

        """
        errors = []

        self.pool.append(self.get_pool(create=False))
        self.pool[-1].label.update(label)

        try:
            self.pool[-1].dmg.exit_status_exception = False
            self.pool[-1].create()
            result_stdout = str(self.pool[-1].dmg.result.stdout)
            exit_status = self.pool[-1].dmg.result.exit_status

            if exit_status == 0 and failure_expected:
                error_message = "dmg pool create is expected to fail, " +\
                    "but worked! {}".format(label)
                errors.append(error_message)
            elif exit_status != 0 and not failure_expected:
                error_message = "dmg pool create failed unexpectedly! " +\
                    "{}".format(label)
                errors.append(error_message)
            elif (exit_status != 0
                  and failure_expected
                  and expected_error not in result_stdout):
                # Failed for the wrong reason.
                error_message = "dmg pool create failed for the wrong " +\
                    "reason! Expected to exist = {}".format(expected_error)
                errors.append(error_message)

        except TestFail as error:
            errors.append(error)
            self.log.info("dmg failed!")

        finally:
            self.pool[-1].dmg.exit_status_exception = True

        return errors

    def test_valid_labels(self):
        """Test ID: DAOS-7942

        Test Description: Create and destroy pool with the following labels.
        * Random alpha numeric string of length 126.
        * Random alpha numeric string of length 127.
        * Random upper case string of length 50.
        * Random lower case string of length 50.
        * Random number string of length 50.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_label
        :avocado: tags=create_valid_labels,test_valid_labels
        """
        self.pool = []
        errors = []
        labels = [
            get_random_string(126),
            get_random_string(127),
            get_random_string(length=50, include=string.ascii_uppercase),
            get_random_string(length=50, include=string.ascii_lowercase),
            get_random_string(length=50, include=string.digits)
        ]

        for label in labels:
            errors.extend(self.verify_create(label, False))
            errors.extend(self.verify_destroy(self.pool[-1], False))

        report_errors(self, errors)

    def test_invalid_labels(self):
        """Test ID: DAOS-7942

        Test Description: Create pool with following invalid labels.
        * UUID format string: 23ab123e-5296-4f95-be14-641de40b4d5a
        * Long label - 128 random chars.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_label
        :avocado: tags=create_invalid_labels,test_invalid_labels
        """
        self.pool = []
        errors = []
        label_outs = [
            ("23ab123e-5296-4f95-be14-641de40b4d5a", "invalid label"),
            (get_random_string(128), "invalid label")
        ]

        for label_out in label_outs:
            errors.extend(self.verify_create(label_out[0], True, label_out[1]))

        report_errors(self, errors)

    def test_duplicate_create(self):
        """Test ID: DAOS-7942

        Test Description:
        1. Create a pool with a label.
        2. Create another pool with the same label. Should fail.
        3. Destroy the pool.
        4. Create a pool with the same label again. It should work this time.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_label
        :avocado: tags=duplicate_label_create,test_duplicate_create
        """
        self.pool = []
        label = "TestLabel"

        # Step 1
        report_errors(self, self.verify_create(label, False))

        # Step 2
        report_errors(self, self.verify_create(label, True, "already exists"))

        # Step 3
        report_errors(self, self.verify_destroy(self.pool[0], False))

        # Step 4
        report_errors(self, self.verify_create(label, False))

    def test_duplicate_destroy(self):
        """Test ID: DAOS-7942

        Test Description:
        1. Create a pool with a label.
        2. Destroy it with the label.
        3. Destroy it with the label again. The second destroy should fail.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_label
        :avocado: tags=duplicate_label_destroy,test_duplicate_destroy
        """
        self.pool = []

        # Step 1
        report_errors(self, self.verify_create("TestLabel", False))

        # Step 2
        report_errors(self, self.verify_destroy(self.pool[-1], False))

        # Step 3
        report_errors(self, self.verify_destroy(self.pool[-1], True, True))

    def test_label_update(self):
        """Test ID: DAOS-7942

        Test Description:
        1. Create a pool.
        2. Update the label with dmg pool set-prop.
        3. Call dmg pool get-prop and verify that the new label is returned.
        4. Try to destroy the pool with the old label. It should fail.
        5. Destroy the pool with the new label. Should work.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_label
        :avocado: tags=label_update,test_label_update
        """
        self.pool = []

        # Step 1
        old_label = "OldLabel"
        report_errors(self, self.verify_create(label=old_label, failure_expected=False))

        # Step 2. Update the label.
        new_label = "NewLabel"
        self.pool[-1].set_property(prop_name="label", prop_value=new_label)
        # Update the label in TestPool.
        self.pool[-1].label.update(new_label)

        # Step 3. Verify the label was set with get-prop.
        prop_value = self.pool[-1].get_property(prop_name="label")
        errors = []
        if prop_value != new_label:
            msg = "Unexpected label from get-prop! Expected = {}; Actual = {}".format(
                new_label, prop_value)
            errors.append(msg)

        # Step 4. Try to destroy the pool with the old label. Should fail.
        self.pool[-1].label.update(old_label)
        errors.extend(self.verify_destroy(pool=self.pool[-1], failure_expected=True))

        # Step 5. Destroy the pool with the new label. Should work.
        self.pool[-1].label.update(new_label)
        errors.extend(self.verify_destroy(pool=self.pool[-1], failure_expected=False))

        report_errors(test=self, errors=errors)
