'''
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers, skipForTicket


class DaosSnapshotTest(TestWithServers):
    """
    Test Class Description:
        Test daos container create-snap, list-snaps, and destroy-snap. Test
        steps:
        1. Create snapshots. Obtain the epoch value each time we create a
            snapshot.
        2. List the snapshots, obtain the epoch values, and compare against
            those returned during create-snap.
        3. Destroy all the snapshots one by one.
        4. List and verify that there's no snapshot.

        Test destroy-snap --epcrange.
        1. Create snapshots.
        2. List the snapshots, obtain the epoch values, and compare against
            those returned during create-snap.
        3. Destroy all the snapshots with --epcrange. Use the first epoch for B
            and the last epoch for E.
        3. List and verify that there's no snapshot.

    Note that we keep the test steps basic due to time constraint. Add more
    cases if we see bugs around this feature.

    :avocado: recursive
    """

    def prepare_pool_container(self):
        """Create a pool and a container and prepare for the test cases."""
        self.add_pool(connect=False)
        self.add_container(self.pool)

    def create_verify_snapshots(self, count):
        """Create and list to verify that the snapshots are created.

        Args:
            count (int): Number of snapshots to create.

        Returns:
            list: Epoch of snapshots created.
        """
        # Create 5 snapshots.
        expected_epochs = [self.container.create_snap()["response"]["epoch"] for _ in range(count)]
        expected_epochs.sort()
        self.log.info("Expected Epochs = %s", expected_epochs)

        # List the snapshots and verify their epochs.
        actual_epochs = [entry["epoch"] for entry in self.container.list_snaps()["response"]]
        actual_epochs.sort()
        self.log.info("Actual Epochs = %s", actual_epochs)
        self.assertEqual(expected_epochs, actual_epochs)

        return actual_epochs

    def test_create_list_delete(self):
        """JIRA ID: DAOS-4872

        Test Description:
            Test daos container create-snap, list-snaps, destroy-snap

        Use Cases:
            See test cases in the class description.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=control,snap,snapshot,daos_cmd
        :avocado: tags=DaosSnapshotTest,daos_snapshot,test_create_list_delete
        """
        self.prepare_pool_container()

        # Create snapshots.
        snapshot_count = self.params.get(
            "snapshot_count", "/run/stress_test/*/")
        self.log.info("Creating %s snapshots", snapshot_count)
        actual_epochs = self.create_verify_snapshots(snapshot_count)

        # Destroy all the snapshots.
        for epoch in actual_epochs:
            self.container.destroy_snap(epc=epoch)

        # List and verify that there's no snapshot.
        epochs = self.container.list_snaps()["response"]
        if len(epochs) > 0:
            self.fail("Expected all container snapshots to be destroyed")

    @skipForTicket("DAOS-4691")
    def test_epcrange(self):
        """JIRA ID: DAOS-4872

        Test Description:
            Test --epcrange. See class description.

        Use Cases:
            See class description.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container,snap,snapshot,daos_cmd
        :avocado: tags=DaosSnapshotTest,daos_snapshot_range,test_epcrange
        """
        self.prepare_pool_container()

        # Create snapshots.
        snapshot_count = self.params.get(
            "snapshot_count", "/run/stress_test/*/")
        self.log.info("Creating %s snapshots", snapshot_count)
        actual_epochs = self.create_verify_snapshots(snapshot_count)

        # Destroy all snapshots with --epcrange.
        epcrange = "{}-{}".format(actual_epochs[0], actual_epochs[-1])
        self.container.destroy_snap(epcrange=epcrange)

        # List and verify that there's no snapshot.
        epochs = self.container.list_snaps()["response"]
        if len(epochs) > 0:
            self.fail("Expected all container snapshots to be destroyed")
