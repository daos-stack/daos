#!/usr/bin/python3
"""
(C) Copyright 2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class HarnessProviderTest(TestWithServers):
    """Provider test cases.

    :avocado: recursive
    """

    def test_provider(self):
        """Test to verify starting servers with different providers.

        This test will verify that starting servers with each provider either passes or is skipped.

        This test can be run in any CI stage: vm, small, medium, large

        :avocado: tags=all
        :avocado: tags=harness,harness_provider_test,provider
        :avocado: tags=test_provider
        """
        self.log.info("Test passed")

    def test_provider_hw(self):
        """Test to verify starting servers with different providers.

        This test will verify that starting servers with each provider either passes or is skipped.

        This test can be run in any CI stage: vm, small, medium, large

        :avocado: tags=all
        :avocado: tags=hw,small,medium,ib2,large
        :avocado: tags=harness,harness_provider_test,provider
        :avocado: tags=test_provider_hw
        """
        self.test_provider()
