#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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
import time
import traceback
import uuid

from apricot import TestWithServers
from pydaos.raw import DaosApiError, DaosContainer


class DeleteContainerTest(TestWithServers):
    """
    Tests DAOS container delete and close.
    :avocado: recursive
    """

    def test_container_delete(self):
        """
        Test basic container delete

        :avocado: tags=all,container,tiny,smoke,full_regression,contdelete
        """
        expected_for_param = []
        uuidlist = self.params.get("uuid",
                                   '/run/createtests/ContainerUUIDS/*/')
        cont_uuid = uuidlist[0]
        expected_for_param.append(uuidlist[1])

        pohlist = self.params.get("poh", '/run/createtests/PoolHandles/*/')
        poh = pohlist[0]
        expected_for_param.append(pohlist[1])

        openlist = self.params.get("opened",
                                   "/run/createtests/ConnectionOpened/*/")
        opened = openlist[0]
        expected_for_param.append(openlist[1])

        forcelist = self.params.get("force", "/run/createtests/ForceDestroy/*/")
        force = forcelist[0]

        # force=0 in .yaml file specifies FAIL, however:
        # if not opened and force=0 expect pass
        if force == 0 and not opened:
            expected_for_param.append('PASS')
        else:
            expected_for_param.append(forcelist[1])

        # opened=True in .yaml file specifies PASS, however
        # if it is also the case force=0, then FAIL is expected

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        # initialize a python pool object then create the underlying
        # daos storage and connect to it
        self.prepare_pool()

        passed = False
        try:
            self.container = DaosContainer(self.context)

            # create should always work (testing destroy)
            if not cont_uuid == 'INVALID':
                cont_uuid = uuid.UUID(uuidlist[0])
                save_cont_uuid = cont_uuid
                self.container.create(self.pool.pool.handle, cont_uuid)
            else:
                self.container.create(self.pool.pool.handle)
                save_cont_uuid = uuid.UUID(self.container.get_uuid_str())

            # Opens the container if required
            if opened:
                self.container.open(self.pool.pool.handle)

            # wait a few seconds and then attempts to destroy container
            time.sleep(5)
            if poh == 'VALID':
                poh = self.pool.pool.handle

            # if container is INVALID, overwrite with non existing UUID
            if cont_uuid == 'INVALID':
                cont_uuid = uuid.uuid4()

            self.container.destroy(force=force, poh=poh, con_uuid=cont_uuid)

            passed = True

        except DaosApiError as excep:
            self.log.info(excep, traceback.format_exc())
            self.container.destroy(force=1, poh=self.pool.pool.handle, con_uuid=save_cont_uuid)

        finally:
            # close container handle, release a reference on pool in client lib
            # Otherwise test will ERROR in tearDown (pool disconnect -DER_BUSY)
            if opened:
                self.container.close()

            self.container = None

            if expected_result == 'PASS' and not passed:
                self.fail("Test was expected to pass but it failed.\n")
            if expected_result == 'FAIL' and passed:
                self.fail("Test was expected to fail but it passed.\n")
