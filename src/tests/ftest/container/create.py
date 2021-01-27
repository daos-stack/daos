#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import print_function

import traceback
import uuid

from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError


class CreateContainerTest(TestWithServers):
    """
    Tests DAOS container create.

    :avocado: recursive
    """

    def test_container_create(self):
        """
        Test ID: DAOS-689

        Test Description: valid and invalid container creation and close.

        :avocado: tags=all,container,tiny,smoke,full_regression,containercreate
        """
        contuuid = None
        expected_results = []

        # setup the pool
        self.prepare_pool()

        # maybe use the good handle, maybe not
        handleparam = self.params.get("handle", '/run/poolhandle/*')
        if handleparam == 'VALID':
            poh = self.pool.pool.handle
        else:
            poh = handleparam
            expected_results.append('FAIL')

        # maybe use a good UUID, maybe not
        uuidparam = self.params.get("uuid", "/uuids/*")
        expected_results.append(uuidparam[1])
        if uuidparam[0] == 'NULLPTR':
            contuuid = 'NULLPTR'
        else:
            contuuid = uuid.UUID(uuidparam[0])

        should_fail = False
        for result in expected_results:
            if result == 'FAIL':
                should_fail = True
                break

        try:
            self.container = DaosContainer(self.context)
            self.container.create(poh, contuuid)

            # check UUID is the specified one
            if (uuidparam[0]).upper() != self.container.get_uuid_str().upper():
                print("uuidparam[0] is {}, uuid_str is {}".format(
                    uuidparam[0], self.container.get_uuid_str()))
                self.fail("Container UUID differs from specified at create\n")

            if should_fail:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if not should_fail:
                self.fail("Test was expected to pass but it failed.\n")
