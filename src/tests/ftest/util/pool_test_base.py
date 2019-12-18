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
from avocado.core.exceptions import TestFail

from apricot import TestWithServers
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

    def create_pool_test(self, namespace=None):
        """Test creating a pool with valid and invalid create arguments."""
        self.pool = TestPool(self.context, debug=True)

        # Optionally find the pool parameters in a different yaml file namespace
        if namespace is not None:
            self.pool.namespace = namespace

        # Get the parameters for the pool create command and determine if this
        # combination of arguments is expected to pass or fail.
        expected_to_pass = True
        for name in self.pool.get_param_names():
            pool_param = getattr(self.pool, name)
            pool_param.get_yaml_value(
                name, self, self.pool.namespace)
            if isinstance(pool_param.value, list) and len(pool_param.value) > 1:
                # The test yaml defines each TestPool parameter as a list of the
                # actual value to be assigned and a boolean indicating if this
                # value will cause the pool create to pass or fail.  Extract the
                # pass/fail state from the parameter list assignment and set the
                # parameter value to the intended value.
                expected_to_pass &= str(pool_param.value[1]).upper() == "PASS"
                if pool_param.value[0] == "VALID":
                    if name == "target_list":
                        pool_param.update([0], name)
                    else:
                        self.fail(
                            "Test needs to be updated with a VALID pool {} "
                            "parameter value".format(name))
                elif pool_param.value[0] == "NULLPTR":
                    pool_param.update(None, name)
                else:
                    pool_param.update(pool_param.value[0], name)

        # Support allowing non-standard TestPool parameters: uid and gid
        for name in ["uid", "gid"]:
            value = self.params.get(
                name, self.pool.namespace, getattr(self.pool, name))
            if isinstance(value, list) and len(value) > 1:
                expected_to_pass &= str(value[1]).upper() == "PASS"
                if value[0] != "VALID":
                    # By default the valid value is set, so only invalid values
                    # need to be assigned
                    setattr(self.pool, name, value[0])
                    self.log.debug("Updated param %s => %s", name, value[0])

        # Attempt to create the pool with the arguments in this test variant
        try:
            self.pool.create()
            exception = None
        except TestFail as error:
            exception = error

        # Verify the result
        if expected_to_pass and exception is not None:
            self.fail(
                "The pool create test was expected to pass, but failed: "
                "{}".format(exception))
        elif not expected_to_pass and exception is None:
            self.fail("The pool create test was expected to fail, but passed")
