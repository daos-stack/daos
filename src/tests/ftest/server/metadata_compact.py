#!/usr/bin/python3
"""
(C) Copyright 2019-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from metadata_test_base import MetadataTestBase


class MetadataCompact(MetadataTestBase):
    """Execute object metadata tests with reduced log compaction.

    Test Class Description:
        Run tests with a reduced number (1) of applied raft log entries that may
        be compacted (default value is 256).

    :avocado: recursive
    """

    def test_metadata_add_remove(self):
        """JIRA ID: DAOS-1512.

        Test Description:
            Verify metadata release the space after container delete.

        Use Cases:
            ?

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=server,metadata,metadata_compact,nvme,metadata_add_remove
        """
        self.create_pool()
        self.container = []

        test_failed = False
        containers_created = []
        for loop in range(10):
            self.log.info("Container Create Iteration %d / 9", loop)
            if not self.create_all_containers():
                self.log.error("Errors during create iteration %d/9", loop)
                test_failed = True

            containers_created.append(len(self.container))

            self.log.info("Container Remove Iteration %d / 9", loop)
            if not self.destroy_all_containers():
                self.log.error("Errors during remove iteration %d/9", loop)
                test_failed = True

        self.log.info("Summary")
        self.log.info("  Loop  Containers Created")
        self.log.info("  ----  ------------------")
        for loop, quantity in enumerate(containers_created):
            self.log.info("  %-4d  %d", loop + 1, quantity)

        if test_failed:
            self.fail("Errors verifying metadata space release")
        self.log.info("Test passed")
