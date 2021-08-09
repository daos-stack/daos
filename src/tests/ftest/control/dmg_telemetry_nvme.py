#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from telemetry_test_base import TestWithTelemetry


class TestWithTelemetryNvme(TestWithTelemetry):
    # pylint: disable=too-many-ancestors
    """Test container telemetry metrics.

    :avocado: recursive
    """

    def test_telemetry_list_nvme(self):
        """JIRA ID: DAOS-7667 / SRS-324.

        Test Description:
            Verify the dmg telemetry list command.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw
        :avocado: tags=control,telemetry,nvme
        :avocado: tags=test_with_telemetry_nvme,test_telemetry_list_nvme
        """
        self.verify_telemetry_list()
