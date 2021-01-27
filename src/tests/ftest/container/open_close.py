#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback
import uuid
from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError


class OpenClose(TestWithServers):
    """
    Tests DAOS container open/close function with handle parameter.
    :avocado: recursive
    """

    def test_closehandle(self):
        """
        Test container close function with container handle parameter.

        :avocado: tags=all,smoke,full_regression,tiny,container,closehandle
        """
        self.container = []
        saved_coh = None

        coh_params = self.params.get("coh",
                                     '/run/container/container_handle/*/')

        expected_result = coh_params[1]

        # initialize a python pool object then create the underlying
        # daos storage and connect to it
        self.prepare_pool()
        poh = self.pool.pool.handle

        try:
            # Container initialization and creation
            self.container.append(DaosContainer(self.context))
            self.container[0].create(poh)
            str_cuuid = self.container[0].get_uuid_str()
            cuuid = uuid.UUID(str_cuuid)
            self.container[0].open(poh, cuuid, 2, None)

            # Defining 'good' and 'bad' container handles
            saved_coh = self.container[0].coh
            if coh_params[0] == 'GOOD':
                coh = self.container[0].coh
            else:
                # create a second container, open to get a handle
                # then close & destroy so handle is invalid
                self.container.append(DaosContainer(self.context))
                self.container[1].create(poh)
                str_cuuid = self.container[1].get_uuid_str()
                cuuid = uuid.UUID(str_cuuid)
                self.container[1].open(poh, cuuid, 2, None)
                coh = self.container[1].coh
                self.container[1].close()
                self.container[1].destroy()

            # close container with either good or bad handle
            self.container[0].close(coh)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            if expected_result == 'PASS':
                print(excep)
                print(traceback.format_exc())
                self.fail("Test was expected to pass but it failed.\n")

            # close above failed so close for real with the right coh
            if saved_coh is not None:
                self.container[0].close(saved_coh)
