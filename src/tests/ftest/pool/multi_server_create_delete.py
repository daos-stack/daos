#!/usr/bin/python3
"""
  (C) Copyright 2017-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from apricot import TestWithServers
from check_for_pool import check_for_pool


RESULT_PASS = "PASS"  # nosec
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

        :avocado: tags=all,full_regression
        :avocado: tags=small
        :avocado: tags=pool,multitarget
        :avocado: tags=multiserver_create_delete
        """
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

        self.add_pool(create=False)
        self.pool.uid = user
        self.pool.gid = group
        self.pool.target_list.update(tgtlist)

        # Disable raising an exception if the dmg command fails
        self.pool.dmg.exit_status_exception = False

        self.pool.create()

        if self.pool.dmg.result.exit_status == 0:
            if expected_result == RESULT_FAIL:
                self.fail(
                    "Test was expected to fail but it passed at pool create.")
            if '0' in tgtlist:
                # check_for_pool checks if the uuid directory exists in host1
                if not check_for_pool(host1, self.pool.uuid):
                    self.fail(
                        "Pool {0} not found on host {1}.\n".format(
                            self.pool.uuid, host1))
            if '1' in tgtlist:
                if not check_for_pool(host2, self.pool.uuid):
                    self.fail(
                        "Pool {0} not found on host {1}.\n".format(
                            self.pool.uuid, host2))
        else:
            test_destroy = False
            self.pool.dmg.exit_status_exception = True

            if expected_result == RESULT_PASS:
                self.fail(
                    "Test was expected to pass but it failed at pool create.")

        self.pool.dmg.exit_status_exception = True

        if test_destroy:
            uuid = self.pool.uuid
            destroy_result = self.pool.destroy()
            if destroy_result:
                if expected_result == RESULT_FAIL:
                    self.fail("Test was expected to fail but it passed at " +
                              "pool create.")
                if '0' in tgtlist:
                    if check_for_pool(host1, self.pool.uuid):
                        self.fail(
                            "Pool {0} found on host {1} after destroy.".format(
                                uuid, host1))
                if '1' in tgtlist:
                    if check_for_pool(host2, self.pool.uuid):
                        self.fail(
                            "Pool {0} found on host {1} after destroy.".format(
                                uuid, host2))
            else:
                if expected_result == RESULT_PASS:
                    self.fail("Test was expected to pass but it failed at " +
                              "pool destroy.")
