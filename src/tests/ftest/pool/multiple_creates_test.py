#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

from apricot import TestWithServers

import check_for_pool


class MultipleCreatesTest(TestWithServers):
    """Tests DAOS pool creation, calling it repeatedly one after another.

    :avocado: recursive
    """

    def verify_pool(self, host, uuid):
        """Verify the pool.

        Args:
            host (str): Server host name
            uuid (str): Pool UUID to verify
        """
        if check_for_pool.check_for_pool(host, uuid.lower()):
            self.fail("Pool {0} not found on host {1}.".format(uuid, host))

    def test_create_one(self):
        """Test issuing a single  pool create commands at once.

        :avocado: tags=all,full_regression
        :avocado: tags=small
        :avocado: tags=pool,smoke,createone
        """
        self.pool = self.get_pool(connect=False)
        print("uuid is {0}\n".format(self.pool.uuid))

        host = self.hostlist_servers[0]
        self.verify_pool(host, self.pool.uuid)

    def test_create_two(self):
        """Test issuing multiple pool create commands at once.

        :avocado: tags=all,full_regression
        :avocado: tags=small
        :avocado: tags=pool,smoke,createtwo
        """
        self.pool = [self.get_pool(connect=False) for _ in range(2)]
        for pool in self.pool:
            print("uuid is {0}\n".format(pool.uuid))

        for host in self.hostlist_servers:
            for pool in self.pool:
                self.verify_pool(host, pool.uuid)

    def test_create_three(self):
        """
        Test issuing multiple pool create commands at once.

        :avocado: tags=all,full_regression
        :avocado: tags=small
        :avocado: tags=pool,createthree
        """
        self.pool = [self.get_pool(connect=False) for _ in range(3)]
        for pool in self.pool:
            print("uuid is {0}\n".format(pool.uuid))

        for host in self.hostlist_servers:
            for pool in self.pool:
                self.verify_pool(host, pool.uuid)
