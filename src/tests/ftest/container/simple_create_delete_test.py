#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
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
