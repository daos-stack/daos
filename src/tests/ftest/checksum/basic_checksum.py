#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers

class ChecksumContainerValidation(TestWithServers):
    """
    Test Class Description: This test is enables
    checksum container properties and performs
    single object inserts and verifies
    contents. This is a basic sanity
    test for enabling checksum testing.
    :avocado: recursive
    """
    # pylint: disable=too-many-instance-attributes
    def setUp(self):
        """Test Setup."""
        super(ChecksumContainerValidation, self).setUp()
        self.agent_sessions = None
        self.pool = None
        self.container = None
        self.records = None

        self.add_pool(connect=False)
        self.pool.connect(2)
        self.add_container(self.pool)

    def test_single_object_with_checksum(self):
        """
        Test ID: DAOS-3927
        Test Description: Write Avocado Test to verify single data after
                          pool/container disconnect/reconnect.
        :avocado: tags=all,full_regression,daily_regression
        :avocado: tags=basic_checksum_object
        """

        self.records = self.params.get("records_qty",
                                       "/run/container/records/*", None)
        self.log.info("Writing the Single Dataset")
        self.pool.get_info()
        if isinstance(self.records, list):
            for rec in self.records:
                self.container.record_qty.update(rec, "record_qty")
                self.log.info(
                    "Wrote %s bytes to container %s",
                    self.container.execute_io(10), self.container.uuid)
        else:
            self.container.record_qty.update(self.records, "record_qty")
            self.log.info(
                "Wrote %s bytes to container %s",
                self.container.execute_io(10), self.container.uuid)
