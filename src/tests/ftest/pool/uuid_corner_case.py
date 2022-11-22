#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class UUIDCornerCase(TestWithServers):
    """Create and destroy a pool with UUID.

    This test covers some corner case and regression regarding UUID usage in
    pool destroy.

    :avocado: recursive
    """
    def verify_destroy_uuid(self):
        """Destroy a pool with UUID and verify that it works.

        Assume that self.pool is configured to use UUID.
        """
        if not self.pool.destroy():
            self.fail("pool isn't defined!")
        self.log.info("pool destroy with UUID succeeded")

    def test_create_label_destroy_uuid(self):
        """Test ID: JIRA-7943

        Test Description: Create with a label, destroy with UUID.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool,uuid,label
        :avocado: tags=uuid_corner_case,create_label_destroy_uuid,test_create_label_destroy_uuid
        """
        # Create with a label - Default.
        self.add_pool(connect=False)

        # Make self.pool use UUID.
        self.pool.use_label = False

        # Destroy with UUID.
        self.verify_destroy_uuid()
