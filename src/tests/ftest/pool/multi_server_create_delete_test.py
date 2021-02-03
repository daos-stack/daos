#!/usr/bin/python
"""
  (C) Copyright 2017-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from apricot import TestWithServers
import check_for_pool

RESULT_PASS = "PASS"
RESULT_FAIL = "FAIL"


class MultiServerCreateDeleteTest(TestWithServers):
    """
    Tests DAOS pool creation, trying both valid and invalid parameters.

    :avocado: recursive
    """

    def test_create(self):
        """Test dmg pool create and destroy with various parameters.

        Create a pool and verify that the pool was created by comparing the
        UUID returned from the dmg command against the directory name in
        /mnt/daos

        Destroy the pool and verify that the directory is deleted.

        :avocado: tags=all,pool,full_regression,small,multitarget
        """
        # Create a dmg command object
        dmg = self.get_dmg_command()

        # Disable raising an exception if the dmg command fails
        dmg.exit_status_exception = False

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        userlist = self.params.get("user", '/run/tests/users/*')
        user = os.getlogin() if userlist[0] == 'valid' else userlist[0]
        expected_for_param.append(userlist[1])

        grouplist = self.params.get("group", '/run/tests/groups/*')
        group = os.getlogin() if grouplist[0] == 'valid' else grouplist[0]
        expected_for_param.append(grouplist[1])

        systemnamelist = self.params.get(
            "systemname", '/run/tests/systemnames/*')
        system_name = systemnamelist[0]
        expected_for_param.append(systemnamelist[1])

        tgtlistlist = self.params.get("tgt", '/run/tests/tgtlist/*')
        tgtlist = tgtlistlist[0]
        expected_for_param.append(tgtlistlist[1])

        # if any parameter is FAIL then the test should FAIL
        expected_result = RESULT_PASS
        if RESULT_FAIL in expected_for_param:
            expected_result = RESULT_FAIL

        host1 = self.hostlist_servers[0]
        host2 = self.hostlist_servers[1]
        test_destroy = True
        data = dmg.pool_create(
            "1GB", user, group, None, tgtlist, None, system_name)
        if dmg.result.exit_status == 0:
            if expected_result == RESULT_FAIL:
                self.fail(
                    "Test was expected to fail but it passed at pool create.")
            if '0' in tgtlist:
                # check_for_pool checks if the uuid directory exists in host1
                exists = check_for_pool.check_for_pool(host1, data["uuid"])
                if exists != 0:
                    self.fail(
                        "Pool {0} not found on host {1}.\n".format(
                            data["uuid"], host1))
            if '1' in tgtlist:
                exists = check_for_pool.check_for_pool(host2, data["uuid"])
                if exists != 0:
                    self.fail(
                        "Pool {0} not found on host {1}.\n".format(
                            data["uuid"], host2))
        else:
            test_destroy = False
            if expected_result == RESULT_PASS:
                self.fail(
                    "Test was expected to pass but it failed at pool create.")

        if test_destroy:
            destroy_result = dmg.pool_destroy(data["uuid"])
            if destroy_result.exit_status == 0:
                if expected_result == RESULT_FAIL:
                    self.fail("Test was expected to fail but it passed at " +
                              "pool create.")
                if '0' in tgtlist:
                    exists = check_for_pool.check_for_pool(host1, data["uuid"])
                    if exists == 0:
                        self.fail(
                            "Pool {0} found on host {1} after destroy.".format(
                                data["uuid"], host1))
                if '1' in tgtlist:
                    exists = check_for_pool.check_for_pool(host2, data["uuid"])
                    if exists == 0:
                        self.fail(
                            "Pool {0} found on host {1} after destroy.".format(
                                data["uuid"], host2))
            else:
                if expected_result == RESULT_PASS:
                    self.fail("Test was expected to pass but it failed at " +
                              "pool destroy.")
