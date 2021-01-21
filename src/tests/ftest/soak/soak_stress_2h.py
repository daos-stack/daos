#!/usr/bin/python
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
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
