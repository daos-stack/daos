#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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
from test_utils import TestPool, TestContainer


class SimpleCreateDeleteTest(TestWithServers):
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

        :avocado: tags=all,container,pr,medium,basecont
        """
        # Create a pool
        self.log.info("Create a pool")
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.create()

        # Check that the pool was created
        self.assertTrue(
            self.pool.check_files(self.hostlist_servers),
            "Pool data not was created")

        # Connect to the pool
        self.pool.connect()

        # Create a container
        self.container = TestContainer(self.pool)
        self.container.get_params(self)
        self.container.create()

	# TODO: the cont info is out of date (see C version daos_cont_info_t)
        # Open and query the container.  Verify the UUID from the query.
        #checks = {
        #    "ci_uuid": self.container.uuid,
        #    "es_hce": 0,
        #    "es_lre": 0,
        #    "es_lhe": 0,
        #    "es_ghce": 0,
        #    "es_glre": 0,
        #    "es_ghpce": 0,
        #    "ci_nsnapshots": 0,
        #    "ci_min_slipped_epoch": 0,
        #}

        #self.assertTrue(
        #    self.container.check_container_info(**checks),
        #    "Error confirming container info from query")

        # Close and destroy the container
        self.container.close()
        self.container.destroy()
