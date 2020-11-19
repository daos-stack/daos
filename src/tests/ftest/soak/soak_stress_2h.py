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


class SoakStress(SoakTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs soak smoke.

    :avocado: recursive
    """

    def test_soak_stress(self):
        """Run all soak tests .

        Test ID: DAOS-2256
        Test ID: DAOS-2509
        Test Description: This will create a slurm batch job that runs
        various jobs defined in the soak yaml
        This test will run soak_stress for 2 hours.

        :avocado: tags=soak,soak_stress_2h
        """
        test_param = "/run/soak_stress/"
        self.run_soak(test_param)
