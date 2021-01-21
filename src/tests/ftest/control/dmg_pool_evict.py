#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
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
                pool=self.pool[0].uuid)
            self.log.info(
                "daos pool list-cont with first pool succeeded as expected")
        except CommandFailure:
            self.fail(
                "daos pool list-cont with first pool failed after pool evict!")

        # Call daos pool list-cont on the evicted pool. It should fail with
        # -1012.
        try:
            daos_cmd.pool_list_cont(
                pool=self.pool[1].uuid)
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
            pool=self.pool[0].uuid)
        self.assertEqual(len(data["uuids"]), len(self.container))
        self.container[0].destroy()
        data = daos_cmd.pool_list_cont(
            pool=self.pool[0].uuid)

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
