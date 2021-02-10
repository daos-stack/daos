#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import print_function

import traceback
import ctypes

from apricot import TestWithServers
from pydaos.raw import DaosApiError


class BadExcludeTest(TestWithServers):
    """Test pool exclude commands with bad parameters.

    :avocado: recursive
    """

    def test_exclude(self):
        """Pass bad parameters to the dmg pool exclude command.

        :avocado: tags=all,full_regression
        :avocado: tags=pool,bad_exclude_test,test_exclude
        """
        # Get the test parameters for the dmg pool exclude command
        #   dmg pool exclude command options]
        #       --pool=         Unique ID of DAOS pool
        #       --rank=         Rank of the targets to be excluded
        #       --target-idx=   Comma-separated list of target idx(s) to be
        #                       excluded from the rank
        uuid = self.params.get("uuid", "/run/test_params/*")
        rank = self.params.get("rank", "/run/test_params/*")
        target = self.params.get("target", "/run/test_params/*")

        # Determine if this test is expected to pass or fail
        expected_result = "PASS"
        for data in (uuid, rank, target):
            if not data[1]:
                expected_result = "FAIL"
                break

        # Create the pool
        self.add_pool(create=True, connect=False)

        # Set arguments for dmg pool exclude
        kwargs = {"pool": uuid[0], "rank": rank[0], "tgt_idx": target[0]}
        for key in kwargs:
            if kwargs[key] == "VALID" and key == "pool":
                kwargs[key] = self.pool.uuid
            elif kwargs[key] == "NULLPTR":
                kwargs[key] = None

        # Attempt the dmg pool exclude command with the specified parameters
        try:
            self.pool.dmg.pool_exclude(**kwargs)
            actual_result = "PASS"
        except CommandFailure as error:
            self.log.info(
                "dmg.pool_exclude(%s) failed: %s",
                ", ".join(
                    ["{}={}".format(key,value) for key, value in kwargs.items()]
                ), error)
            actual_result = "FAIL"

        if actual_result != expected_result:
            self.fail(
                "Test was expected to {} but {}ED".format(
                    expected_result, actual_result))
        self.log.info("Test PASSED with expected result")
