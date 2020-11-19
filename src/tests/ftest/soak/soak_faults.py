#!/usr/bin/python
"""
(C) Copyright 2018-2019 Intel Corporation.

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

from soak_test_base import SoakTestBase


class SoakFaultInject(SoakTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs soak with fault injectors.

    :avocado: recursive
    """

    def test_soak_faults(self):
        """Run all soak tests with fault injectors.

        Test ID: DAOS-2511
        Test Description: This will create a soak job that runs
        various fault injectors  defined in the soak yaml
        This test will run for the time specified by test_timeout in
        the soak_faults config file.

        :avocado: tags=soak,soak_faults
        """
        test_param = "/run/soak_faults/"
        self.run_soak(test_param)
