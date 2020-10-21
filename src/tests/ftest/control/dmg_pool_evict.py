#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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
from apricot import TestWithServers
from command_utils import CommandFailure


class DmgPoolEvictTest(TestWithServers):
    """Test Class Description:
    This test covers the following requirement.
    (SRS-12-0114) All connections to a specific pool can be revoked via the dmg
    tool

    Test step:
    1. Create 2 pools.
    2. Create a container in each pool.
    3. Call dmg pool evict on the second pool.
    4. Call daos pool list-cont on the evicted pool. It should fail with -1012.
    5. Verify the other pool isn't affected by creating a new container, list,
    and destroy.

    :avocado: recursive
    """

    def test_dmg_pool_evict(self):
        """
        JIRA ID: DAOS-5101

        Test Description: Test dmg pool evict.

        :avocado: tags=all,small,full_regression,control,dmg_pool_evict
        """
        # Create 2 pools and create a container in each pool.
        self.pool = []
        self.container = []
        for _ in range(2):
            self.pool.append(self.get_pool())
            self.container.append(self.get_container(self.pool[-1]))

        # Call dmg pool evict on the second pool.
        self.get_dmg_command().pool_evict(pool=self.pool[1].uuid)

        # Call daos pool list-cont on the first pool. It should succeed.
        daos_cmd = self.get_daos_command()
        try:
            daos_cmd.pool_list_cont(
                pool=self.pool[0].uuid, svc=self.pool[0].svc_ranks[0])
            self.log.info(
                "daos pool list-cont with first pool succeeded as expected")
        except CommandFailure:
            self.fail(
                "daos pool list-cont with first pool failed after pool evict!")

        # Call daos pool list-cont on the evicted pool. It should fail with
        # -1012.
        try:
            daos_cmd.pool_list_cont(
                pool=self.pool[1].uuid, svc=self.pool[1].svc_ranks[0])
            self.fail(
                "daos pool list-cont with second pool succeeded after pool " +
                "evict!")
        except CommandFailure:
            self.log.info(
                "daos pool list-cont with second pool failed as expected")

        self.container.pop()

        # Verify the other pool isn't affected by creating a new container,
        # list, and destroy.
        self.container.append(self.get_container(self.pool[0]))
        data = daos_cmd.pool_list_cont(
            pool=self.pool[0].uuid, svc=self.pool[0].svc_ranks[0])
        self.assertEqual(len(data["uuids"]), len(self.container))
        self.container[0].destroy()
        data = daos_cmd.pool_list_cont(
            pool=self.pool[0].uuid, svc=self.pool[0].svc_ranks[0])

        # Create list of container UUIDs and compare. Convert command output to
        # upper case since we use upper case in object.
        uuids_cmd = []
        for uuid in data["uuids"]:
            uuids_cmd.append(uuid.upper())
        uuids_obj = []
        for cont in self.container[1:]:
            uuids_obj.append(cont.uuid)
        self.assertEqual(
            uuids_cmd, uuids_obj,
            "Container UUIDs in pool {} do not match! Cmd: {}, Obj: {}".format(
                self.pool[0].uuid, uuids_cmd, uuids_obj)
        )
