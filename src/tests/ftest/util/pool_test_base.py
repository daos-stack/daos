#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from itertools import product

from avocado.core.exceptions import TestFail

from apricot import TestWithServers
from command_utils import BasicParameter
from test_utils import TestPool


class PoolTestBase(TestWithServers):
    """Defines common methods for pool tests.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Generate separate log files per test variant
        self.update_log_file_names()
        super(PoolTestBase, self).setUp()

    def get_param_list(self, name):
        """Get the list of param values and expected result for the param name.

        Args:
            name (str): parameter name

        Returns:
            list: a list of lists containing the parameter name, value, and a
                boolean indicating whether or not this value will succeed in
                creating a pool.

        """
        # Get the default value for the parameter to use if a list is not set
        default_value = getattr(self.pool, name)
        if isinstance(default_value, BasicParameter):
            default_value = default_value.value

        # Get the list of parameter values and pass/fail state from the yaml
        param_lists = self.params.get(
            "{}_list".format(name),
            self.pool.namespace,
            [[default_value, "PASS"]]
        )

        # Process the list data
        for param_list in param_lists:
            # Include the parameter name with the value and expected result
            param_list.insert(0, name)

            # Replace any keyword values
            if param_list[1] == "VALID" and name == "target_list":
                param_list[1] = [0]
            elif param_list[1] == "VALID" and name.endswith("id"):
                param_list[1] = default_value
            elif param_list[1] == "NULLPTR":
                param_list[1] = None

            # Convert the PASS/FAIL keywords into a boolean
            param_list[2] = str(param_list[2]).upper() == "PASS"

        return param_lists

    def run_pool_create_test(self, namespace=None):
        """Run all test variants of the pool create test.

        Args:
            namespace (str, optional): [description]. Defaults to None.
        """
        # Create a TestPool object
        self.pool = TestPool(self.context, debug=True)
        if namespace is not None:
            self.pool.namespace = namespace
        self.pool.get_params(self)

        # Obtain the lists of arguments to use
        param_names = (
            "mode",
            "uid",
            "gid",
            "scm_size",
            "name",
            "target_list",
            "svcn",
            "nvme_size"
        )
        param_lists = [self.get_param_list(key) for key in param_names]

        # Determine the number of test variants
        test_variants = list(product(*param_lists))
        for variant in test_variants:
            self.log.debug("  %s", variant)
        total = len(test_variants)
        self.log.info("Testing %s different pool create variants", total)

        # Run all the variants of the test
        error_count = 0
        for index, test in enumerate(test_variants):
            self.log.info("[%03d/%03d] - START TEST VARIANT", index + 1, total)

            # Set the pool attributes
            expected_to_pass = True
            for data in test:
                pool_attribute = getattr(self.pool, data[0])
                if isinstance(pool_attribute, BasicParameter):
                    pool_attribute.update(data[1], data[0])
                else:
                    pool_attribute = data[1]
                    self.log.debug("Updated param %s => %s", data[0], data[1])
                expected_to_pass &= data[2]
            self.log.info(
                "[%03d/%03d] - expected to pass: %s",
                index + 1, total, str(expected_to_pass))

            # Attempt to create the pool
            try:
                self.pool.create()
                exception = None
            except TestFail as error:
                exception = error

            # Verify the result
            if expected_to_pass and exception is not None:
                error_count += 1
                self.log.error(
                    "[%03d/%03d] - The pool create test was expected to pass, "
                    "but failed: %s", index + 1, total, exception)
            elif not expected_to_pass and exception is None:
                error_count += 1
                self.log.error(
                    "[%03d/%03d] - The pool create test was expected to fail, "
                    "but passed", index + 1, total)

            # Destroy any successful pools
            if not exception:
                self.pool.destroy()

        # Determine if the overall test passed
        if error_count > 0:
            self.fail(
                "Detected {} error(s) creating {} pools".format(
                    error_count, total))
