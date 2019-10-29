#!/usr/bin/python
'''
  (C) Copyright 2017-2019 Intel Corporation.

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
'''
from __future__ import print_function

import time
from avocado import main
from apricot import TestWithServers
from test_utils import TestPool, TestContainer
from pydaos.raw import c_uuid_to_str


# pylint: disable=broad-except
class SimpleCreateDeleteTest(TestWithServers):
    """Tests container basics including create, destroy, open, query and close.

    :avocado: recursive
    """

    def test_container_basics(self):
        """
        Test basic container create/destroy/open/close/query.

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
            "Pool data not was not created")

        # Connect to the pool
        self.assertTrue(self.pool.connect(1), "Pool connect failed")

        # Create a container
        self.container = TestContainer(self.pool)
        self.container.get_params(self)
        self.container.create()

        self.assertTrue(self.container.open(), "Container open failed")

        # Query and compare the UUID returned from create with
        # that returned by query
        self.container.container.query()
        if self.container.container.get_uuid_str() != c_uuid_to_str(
                self.container.container.info.ci_uuid):
            self.fail("Container UUID did not match the one in info'n")

        self.assertTrue(self.container.close(), "Container close failed")

        # Wait and destroy
        time.sleep(5)
        self.assertTrue(self.container.destroy(), "Container destroy failed")


if __name__ == "__main__":
    main()
