"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import string
import uuid

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from exception_utils import CommandFailure
from general_utils import get_random_string, report_errors


class ContainerLabelTest(TestWithServers):
    """Test container create and destroy operations with labels.

    :avocado: recursive
    """

    def test_container_label_valid(self):
        """Test ID: DAOS-11272

        Test Description: Create and destroy container with valid labels:
        * UUID only - no label.
        * Random alpha numeric string of length 126.
        * Random alpha numeric string of length 127.
        * Random upper case string of length 50.
        * Random lower case string of length 50.
        * Random number string of length 50.
        * Valid label but destroy with UUID.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=ContainerLabelTest,test_container_label_valid
        """
        pool = self.get_pool()
        for label in (
                None,
                get_random_string(126),
                get_random_string(127),
                get_random_string(length=50, include=string.ascii_uppercase),
                get_random_string(length=50, include=string.ascii_lowercase),
                get_random_string(length=50, include=string.digits)):
            container = self.get_container(pool, label=label)
            container.destroy()

        self.log.info('Creating a container with a label and destroying with the UUID')
        container = self.get_container(pool, label='cont')
        container.use_label = False
        container.destroy()

    def test_container_label_invalid(self):
        """Test ID: DAOS-11272

        Test Description: Create container with invalid labels:
        * The default string for an unset container label property.
        * UUID format string. E.g.: 23ab123e-5296-4f95-be14-641de40b4d5a
        * Long label - 128 random chars.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=ContainerLabelTest,test_container_label_invalid
        """
        errors = []
        pool = self.get_pool()
        for label, expected_error in (
                ('container_label_not_set', 'DER_INVAL'),
                (str(uuid.uuid4()), 'invalid label'),
                (get_random_string(128), 'invalid label')):
            try:
                self.get_container(pool, label=label)
            except TestFail as error:
                if expected_error not in str(error):
                    errors.append(
                        'Expected container create to fail with "{}" for label {}'.format(
                            expected_error, label))
            else:
                errors.append('Expected container create to fail for label "{}"'.format(label))
        report_errors(self, errors)

    def test_container_label_duplicate(self):
        """Test ID: DAOS-11272

        Test Description:
        1. Create a container with a label.
        2. Try to create another container with the same label and expect a failure.
        3. Destroy the container.
        4. Create a container with the same label and expect it to pass.
        5. Destroy the container and expect it to pass.
        6. Try to destroy the container with the same label again and expect it to fail.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=ContainerLabelTest,test_container_label_duplicate
        """
        pool = self.get_pool()
        label = 'cont'  # Must override label from get_params()

        # Create a container
        container = self.get_container(pool, label=label)

        self.log.info('Creating a duplicate container with label %s', label)
        expected_error = 'DER_EXIST'
        try:
            self.get_container(pool, label=label)
        except TestFail as error:
            if expected_error not in str(error):
                self.fail(
                    'Expected container create to fail with "{}" for label {}'.format(
                        expected_error, label))
        else:
            self.fail('Expected container create to fail with duplicate label')

        self.log.info('Destroying and re-creating container with label %s', label)
        container.destroy()
        try:
            container = self.get_container(pool, label=label)
        except TestFail:
            self.fail('Expected container create to succeed after destroying original')

        self.log.info('Destroying container twice with label %s', label)
        expected_error = 'DER_NONEXIST'
        container.destroy()
        try:
            container.daos.container_destroy(pool=pool.identifier, cont=container.identifier)
        except CommandFailure as error:
            if expected_error not in str(error):
                self.fail(
                    'Expected container destroy to fail with "{}" for label {}'.format(
                        expected_error, label))
        else:
            self.fail('Expected container destroy to fail on already destroyed container')

    def test_container_label_update(self):
        """Test ID: DAOS-11272

        Test Description:
        1. Create a container.
        2. Update the label with daos container set-prop.
        3. Verify daos container get-prop returns the new label
        4. Verify destroying with the old label fails.
        5. Verify destroying with the new label works.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=ContainerLabelTest,test_container_label_update
        """
        pool = self.get_pool()
        original_label = 'original_label'
        new_label = 'new_label'

        # Create with initial label and update to the new label
        container = self.get_container(pool, label=original_label)
        container.set_prop(prop='label', value=new_label)
        container.update_params(label=new_label)

        # Verify get-prop returns the correct label
        container.use_label = False
        if not container.verify_prop({'label': new_label}):
            self.fail('get-prop returned incorrect label')
        container.use_label = True

        # Verify query with the new label
        container.query()

        self.log.info('Attempting to destroy container with old label')
        expected_error = 'DER_NONEXIST'
        try:
            container.daos.container_destroy(pool=pool.identifier, cont=original_label)
        except CommandFailure as error:
            if expected_error not in str(error):
                self.fail(
                    'Expected container destroy to fail with "{}" for label {}'.format(
                        expected_error, original_label))
        else:
            self.fail('Expected container destroy to fail when using old label')

        # Destroy with the new label
        container.destroy()
