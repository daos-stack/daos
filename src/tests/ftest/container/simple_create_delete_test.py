#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from test_utils_pool import TestPool
from test_utils_container import TestContainer


class SimpleCreateDeleteTest(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Tests container basics including create, destroy, open, query and close.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a SimpleCreateDeleteTest object."""
        super(SimpleCreateDeleteTest, self).__init__(*args, **kwargs)
        self.pool = None
        self.container = None

    def test_container_basics(self):
        """Test basic container create/destroy/open/close/query.

        :avocado: tags=all,container,pr,daily_regression,medium,basecont
        """
        # Create a pool
        self.log.info("Create a pool")
        self.prepare_pool()

        # Check that the pool was created
        self.assertTrue(
            self.pool.check_files(self.hostlist_servers),
            "Pool data was not created")

        # Create a container
        self.container = TestContainer(self.pool)
        self.container.get_params(self)
        self.container.create()

        # TO Be Done:the cont info needs update (see C version daos_cont_info_t)
        # Open and query the container.Verify the UUID & No of Snapshot.
        checks = {
            "ci_uuid": self.container.uuid,
            "ci_nsnapshots": 0}

        self.assertTrue(self.container.check_container_info(**checks),
                        "Error confirming container info from query")

        # Close and destroy the container
        self.container.close()
        self.container.destroy()
