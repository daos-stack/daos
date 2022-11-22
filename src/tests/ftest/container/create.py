#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''


import traceback

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

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container,smoke
        :avocado: tags=container_create,test_container_create
        """
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

        should_fail = False
        for result in expected_results:
            if result == 'FAIL':
                should_fail = True
                break

        try:
            self.container = DaosContainer(self.context)
            self.container.create(poh)

            if self.container.uuid is None:
                self.fail("Container uuid is None.")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if not should_fail:
                self.fail("Test was expected to pass but it failed.\n")
