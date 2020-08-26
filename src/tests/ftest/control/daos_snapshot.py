#!/usr/bin/python
'''
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
'''
from apricot import TestWithServers, skipForTicket
from daos_utils import DaosCommand
from test_utils_container import TestContainer


class DaosSnapshotTest(TestWithServers):
    """
    Test Class Description:
        Test daos container create-snap, list-snaps, and destroy-snap. Test
        steps:
        1. Create 5 snapshots. Obtain the epoch value each time we create a
            snapshot.
        2. List the 5 snapshots, obtain the epoch values, and compare against
            those returned during create-snap.
        3. Destroy all the snapshots one by one.
        4. List and verify that there's no snapshot.

        Test destroy-snap --epcrange.
        1. Create 5 snapshots.
        2. Destory all the snapshots with --epcrange. Use the first epoch for B
            and the last epoch for E.
        3. List and verify that there's no snapshot.

    Note that we keep the test steps basic due to time constraint. Add more
    cases if we see bugs around this feature.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a DaosSnapshotTest object."""
        super(DaosSnapshotTest, self).__init__(*args, **kwargs)
        self.daos_cmd = None
        self.svc = None

    def create_snapshot(self, pool_uuid, svc, cont_uuid, count):
        """Create snapshots and return the epoch values obtained from stdout.

        Args:
            pool_uuid (String): Pool UUID.
            svc (String): Service replicas.
            cont_uuid (String): Container UUID.
            count (Integer): Number of snapshots to create.

        Returns:
            List of string: Epochs obtained from stdout.
        """
        epochs = []
        for _ in range(count):
            epochs.append(
                self.daos_cmd.container_create_snap(pool=pool_uuid,
                cont=cont_uuid, svc=svc)["epoch"])
        return epochs

    def prepare_pool_container(self):
        """Create a pool and a container and prepare for the test cases.
        """
        self.daos_cmd = DaosCommand(self.bin)
        self.add_pool(connect=False)
        self.svc = ",".join(str(rank) for rank in self.pool.svc_ranks)
        self.add_container(self.pool)

    def test_create_list_delete(self):
        """JIRA ID: DAOS-4872

        Test Description:
            Test daos container create-snap, list-snaps, destroy-snap

        Use Cases:
            See test cases in the class description.

        :avocado: tags=all,small,control,full_regression,daos_snapshot
        """
        self.prepare_pool_container()

        # 1. Create 5 snapshots.
        expected_epochs = self.create_snapshot(
            pool_uuid=self.pool.uuid, svc=self.svc, cont_uuid=self.container.uuid,
            count=5)
        expected_epochs.sort()
        self.log.debug("Expected Epochs = {}".format(expected_epochs))

        # 2. List the 5 snapshots and verify their epochs.
        actual_epochs = self.daos_cmd.container_list_snaps(
            pool=self.pool.uuid, cont=self.container.uuid,
            svc=self.svc)["epochs"]
        actual_epochs.sort()
        self.log.debug("Actual Epochs = {}".format(actual_epochs))
        self.assertEqual(expected_epochs, actual_epochs)

        # 3. Destroy all the snapshots.
        for epoch in actual_epochs:
            self.daos_cmd.container_destroy_snap(
                pool=self.pool.uuid, cont=self.container.uuid, epc=epoch,
                svc=self.svc)

        # 4. List and verify that there's no snapshot.
        epochs = self.daos_cmd.container_list_snaps(
            pool=self.pool.uuid, cont=self.container.uuid, svc=self.svc)
        self.assertTrue(not epochs)

    @skipForTicket("DAOS-4691")
    def test_epcrange(self):
        """JIRA ID: DAOS-4872

        Test Description:
            Test --epcrange. See class description.

        Use Cases:
            See class description.
        
        :avocado: tags=all,small,container,full_regression,daos_snapshot_range
        """
        self.prepare_pool_container()

        # 1. Create 5 snapshots.
        new_epochs = self.create_snapshot(
            pool_uuid=self.pool.uuid, svc=self.svc,
            cont_uuid=self.container.uuid, count=5)

        # 2. Destroy all snapshots with --epcrange.
        self.daos_cmd.container_destroy_snap(
            pool=self.pool.uuid, cont=self.container.uuid,
            epcrange=[new_epochs[0], new_epochs[-1]], svc=self.svc)

        # 3. List and verify that there's no snapshot.
        epochs = self.daos_cmd.container_list_snaps(
            pool=self.pool.uuid, cont=self.container.uuid, svc=self.svc)
        self.assertTrue(not epochs)
