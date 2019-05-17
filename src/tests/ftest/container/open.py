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
import time
import traceback
import uuid

from avocado import main
from apricot import TestWithServers

from daos_api import DaosPool, DaosContainer, DaosApiError

# pylint: disable=too-many-instance-attributes
class OpenContainerTest(TestWithServers):
    """
    Tests DAOS container bad create (non existing pool handle, bad uuid)
    and close.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        super(OpenContainerTest, self).__init__(*args, **kwargs)
        self.pool1 = None
        self.pool2 = None
        self.container1 = None
        self.container2 = None

    def setUp(self):
        super(OpenContainerTest, self).setUp()

        # common parameters used in pool create
        self.createmode = self.params.get("mode",
                                          '/run/createtests/createmode/')
        self.createsetid = self.params.get("setname",
                                           '/run/createtests/createset/')
        self.createsize = self.params.get("size",
                                          '/run/createtests/createsize/')

        # pool 1 UID GID
        self.createuid1 = self.params.get("uid", '/run/createtests/createuid1/')
        self.creategid1 = self.params.get("gid", '/run/createtests/creategid1/')

        # pool 2 UID GID
        self.createuid2 = self.params.get("uid", '/run/createtests/createuid2/')
        self.creategid2 = self.params.get("gid", '/run/createtests/creategid2/')

    def tearDown(self):
        try:
            if self.container1 is not None:
                self.container1.destroy()
            if self.container2 is not None:
                self.container2.destroy()
            if self.pool1 is not None and self.pool1.attached:
                self.pool1.destroy(1)
            if self.pool2 is not None and self.pool2.attached:
                self.pool2.destroy(1)
        finally:
            super(OpenContainerTest, self).tearDown()

    def test_container_open(self):
        """
        Test basic container bad create.

        :avocado: tags=container,containeropen
        """
        container_uuid = None
        expected_for_param = []
        uuidlist = self.params.get("uuid", '/run/createtests/uuids/*/')
        container_uuid = uuidlist[0]
        expected_for_param.append(uuidlist[1])

        pohlist = self.params.get("poh", '/run/createtests/handles/*/')
        poh = pohlist[0]
        expected_for_param.append(pohlist[1])

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        try:
            # create two pools and try to create containers in these pools
            self.pool1 = DaosPool(self.context)
            self.pool1.create(self.createmode, self.createuid1, self.creategid1,
                              self.createsize, self.createsetid, None)

            self.pool2 = DaosPool(self.context)
            self.pool2.create(self.createmode, self.createuid2, self.creategid2,
                              self.createsize, None, None)

            # Connect to the pools
            self.pool1.connect(1 << 1)
            self.pool2.connect(1 << 1)

            # defines pool handle for container open
            if pohlist[0] == 'pool1':
                poh = self.pool1.handle
            else:
                poh = self.pool2.handle

            # Create a container in pool1
            self.container1 = DaosContainer(self.context)
            self.container1.create(self.pool1.handle)

            # defines test UUID for container open
            if uuidlist[0] == 'pool1':
                struuid = self.container1.get_uuid_str()
                container_uuid = uuid.UUID(struuid)
            else:
                if uuidlist[0] == 'MFUUID':
                    container_uuid = "misformed-uuid-0000"
                else:
                    container_uuid = uuid.uuid4() # random uuid

            # tries to open the container1
            # open should be ok only if poh = pool1.handle &&
            #                           containerUUID = container1.uuid
            self.container1.open(poh, container_uuid)

            # wait a few seconds and then destroy containers
            time.sleep(5)
            self.container1.close()
            self.container1.destroy()
            self.container1 = None

            # cleanup the pools
            self.pool1.disconnect()
            self.pool1.destroy(1)
            self.pool1 = None
            self.pool2.disconnect()
            self.pool2.destroy(1)
            self.pool2 = None

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
        finally:
            if self.hostfile is not None:
                os.remove(self.hostfile)

if __name__ == "__main__":
    main()
