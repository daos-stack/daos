#!/usr/bin/python
'''
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
'''
from apricot import TestWithServers
from dmg_utils import get_pool_uuid_service_replicas_from_stdout
from daos_utils import DaosCommand


class ListContainerTest(TestWithServers):
    """
    Test Class Description:
        Tests daos pool list-cont. Test the following cases.
        1. Create 1 container, list, and verify the listed container UUID
            matches the UUID returned when created.
        2. Create 1 more container and verify list; 2 total containers
        3. Create 98 more containers and verify list; 100 total containers
        4. Create 2 additional pools and create 10 containers in each pool.
        Verify the container UUIDs in these 2 new pools.

    :avocado: recursive
    """
    def verify_uuids(self, pool_uuid, service_replicas, expected_uuids):
        """Call daos pool list-cont to list the containers in the given pool
        UUID and verify the output against the given expected container UUIDs

        Args:
            pool_uuid (String): Pool UUID to get the list of containers
            service_replicas (String): Service replicas. If there are multiple,
                they're separated by comma like 1,2,3
            expected_uuids (List of string): Expected container UUIDs
        """
        expected_uuids.sort()
        # daos pool list-cont returns the date, host name, and container UUID
        # as below:
        # 03/31-21:32:24.53 wolf-3 2f69b198-8478-472e-b6c8-02a451f4de1b
        list_stdout = self.daos_cmd.pool_list_cont(
            pool_uuid, service_replicas).stdout
        actual_uuids = []
        lines = list_stdout.splitlines()
        for cont_info in lines:
            actual_uuids.append(cont_info.split()[2])
        actual_uuids.sort()
        self.assertEqual(expected_uuids, actual_uuids)

    def test_list_container(self):
        """Jira ID: DAOS-3629

        Test Description:
            Test daos pool list-cont

        Use Cases:
            See test cases in the class description.

        :avocado: tags=all,container,full_regression,list_containers
        """
        # 1. Create 1 container and list
        expected_uuids1 = []
        stdoutput = self.get_dmg_command().pool_create(scm_size="150MB").stdout
        uuid1, service_replicas1 = \
            get_pool_uuid_service_replicas_from_stdout(stdoutput)
        self.daos_cmd = DaosCommand(self.bin)
        create_stdout = self.daos_cmd.container_create(
            pool=uuid1, svc=service_replicas1).stdout
        expected_uuids1.append(create_stdout.split()[3])
        self.verify_uuids(uuid1, service_replicas1, expected_uuids1)

        # 2. Create 1 more container and list; 2 total
        create_stdout = self.daos_cmd.container_create(
            pool=uuid1, svc=service_replicas1).stdout
        expected_uuids1.append(create_stdout.split()[3])
        self.verify_uuids(uuid1, service_replicas1, expected_uuids1)

        # 3. Create 98 more containers and list; 100 total
        for _ in range(98):
            create_stdout = self.daos_cmd.container_create(
                pool=uuid1, svc=service_replicas1).stdout
            expected_uuids1.append(create_stdout.split()[3])
        self.verify_uuids(uuid1, service_replicas1, expected_uuids1)

        # 4. Create 2 additional pools and create 10 containers in each pool
        stdoutput = self.get_dmg_command().pool_create(scm_size="150MB").stdout
        uuid2, service_replicas2 = \
            get_pool_uuid_service_replicas_from_stdout(stdoutput)
        stdoutput = self.get_dmg_command().pool_create(scm_size="150MB").stdout
        uuid3, service_replicas3 = \
            get_pool_uuid_service_replicas_from_stdout(stdoutput)
        # Create 10 containers in pool 2 and verify
        expected_uuids2 = []
        for _ in range(10):
            create_stdout = self.daos_cmd.container_create(
                pool=uuid2, svc=service_replicas2).stdout
            expected_uuids2.append(create_stdout.split()[3])
        self.verify_uuids(uuid2, service_replicas2, expected_uuids2)
        # Create 10 containers in pool 3 and verify
        expected_uuids3 = []
        for _ in range(10):
            create_stdout = self.daos_cmd.container_create(
                pool=uuid3, svc=service_replicas3).stdout
            expected_uuids3.append(create_stdout.split()[3])
        self.verify_uuids(uuid3, service_replicas3, expected_uuids3)
