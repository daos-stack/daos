#!/usr/bin/python
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
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
