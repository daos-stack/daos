#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random

from telemetry_test_base import TestWithTelemetry


class TestWithTelemetryBasic(TestWithTelemetry):
    # pylint: disable=too-many-ancestors
    """Test basic telemetry metrics.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super().__init__(*args, **kwargs)

    def test_telemetry_list(self):
        """JIRA ID: DAOS-7667 / SRS-324.

        Test Description:
            Verify the dmg telemetry list command.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,telemetry
        :avocado: tags=test_with_telemetry_basic,test_telemetry_list
        """
        self.verify_telemetry_list()
