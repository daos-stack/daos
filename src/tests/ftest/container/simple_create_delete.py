"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class SimpleCreateDeleteTest(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Tests container basics including create, destroy, open, query and close.

    :avocado: recursive
    """

    def test_container_basics(self):
        """Test basic container create/destroy/open/close/query.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=SimpleCreateDeleteTest,test_container_basics
        """
        # Create a pool
        self.log.info("Create a pool")
        pool = self.get_pool()

        # Check that the pool was created
        self.assertTrue(
            pool.check_files(self.hostlist_servers),
            "Pool data was not created")

        # Create a container
        container = self.get_container(pool)

        # TO Be Done:the cont info needs update (see C version daos_cont_info_t)
        # Open and query the container.Verify the UUID & No of Snapshot.
        checks = {
            "ci_uuid": container.uuid,
            "ci_nsnapshots": 0}

        self.assertTrue(container.check_container_info(**checks),
                        "Error confirming container info from query")

        # Close and destroy the container
        container.close()
        container.destroy()
