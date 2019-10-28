#!/usr/bin/python
'''
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
'''
from __future__ import print_function

import os
import traceback
import uuid
from apricot import TestWithServers

from pydaos.raw import DaosPool, DaosContainer, DaosApiError


class OpenClose(TestWithServers):
    """
    Tests DAOS container open/close function with handle parameter.
    :avocado: recursive
    """

    def test_closehandle(self):
        """
        Test container close function with container handle paramter.

        :avocado: tags=all,smoke,full_regression,tiny,container,closehandle
        """
        self.container = []
        saved_coh = None

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/pool/createmode/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/pool/createset/')
        createsize = self.params.get("size", '/run/pool/createsize/')
        coh_params = self.params.get("coh",
                                     '/run/container/container_handle/*/')

        expected_result = coh_params[1]

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)

            poh = self.pool.handle
            self.pool.connect(1 << 1)

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
