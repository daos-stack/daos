"""
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from rebuild_test_base import RebuildTestBase


class RbldEpoch(RebuildTestBase):
    """Tests for rebuild with epoch/snapshot operations.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a RbldEpoch object."""
        super().__init__(*args, **kwargs)
        self.snap_epochs = []

    def test_rebuild_with_snapshot_epoch(self):
        """JIRA ID: DAOS-18696.

        Test Description:
            Write data to a container, create a snapshot to preserve the epoch,
            write additional data, trigger rebuild by excluding a rank, then
            verify that data at the snapshot epoch is still accessible after
            rebuild completes.

        Use Cases:
            Rebuild preserves snapshot epoch data integrity.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,epoch,snapshot
        :avocado: tags=RbldEpoch,test_rebuild_with_snapshot_epoch
        """
        obj_class = self.params.get("object_class", "/run/rebuild/*")
        rank = self.params.get("rank", "/run/rebuild/*")

        self.log_step("Setup pool and container")
        self.setup_test_pool()
        self.setup_test_container()
        self.create_test_pool()

        self.log_step("Create container and write initial objects")
        self.container.create()
        self.container.write_objects(rank, obj_class)

        self.log_step("Create snapshot to capture epoch")
        self.container.create_snap()
        snap_epoch = self.container.epoch
        self.assertIsNotNone(snap_epoch, "Snapshot epoch should not be None after create_snap")
        self.log.info("Captured snapshot at epoch %s", snap_epoch)

        self.log_step("Write additional objects after snapshot")
        self.container.write_objects(obj_class=obj_class)

        self.log_step("Verify initial rank has objects before rebuild")
        self.verify_rank_has_objects()

        self.log_step(f"Exclude rank {rank} to trigger rebuild")
        self.start_rebuild()

        self.log_step("Wait for rebuild to complete")
        self.pool.wait_for_rebuild_to_end(interval=1)

        self.log_step("Restore container status to healthy")
        self.container.set_prop(prop="status", value="healthy")

        self.log_step("Verify data at snapshot epoch is accessible after rebuild")
        self.verify_container_data(txn=snap_epoch)

        self.log_step("Verify latest data is also accessible after rebuild")
        self.verify_container_data()

        self.log_step("Verify pool info after rebuild")
        self.update_pool_verify()
        self.execute_pool_verify(" after rebuild with snapshot epoch")

        self.log_step("Test Passed")

    def test_rebuild_with_multiple_epochs(self):
        """JIRA ID: DAOS-18696.

        Test Description:
            Write data in multiple rounds, capturing a snapshot epoch after each
            round. Trigger rebuild by excluding a rank, then verify that all
            captured snapshot epochs remain accessible and return correct data
            after rebuild completes.

        Use Cases:
            Rebuild preserves multiple historical snapshot epochs.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,epoch,snapshot,multiepoch
        :avocado: tags=RbldEpoch,test_rebuild_with_multiple_epochs
        """
        obj_class = self.params.get("object_class", "/run/rebuild/*")
        rank = self.params.get("rank", "/run/rebuild/*")
        num_snapshots = self.params.get("num_snapshots", "/run/testparams/*", default=3)

        self.log_step("Setup pool and container")
        self.setup_test_pool()
        self.setup_test_container()
        self.create_test_pool()
        self.container.create()

        self.snap_epochs = []
        for i in range(num_snapshots):
            self.log_step(f"Write objects round {i + 1}/{num_snapshots}")
            self.container.write_objects(rank if i == 0 else None, obj_class)

            self.log_step(f"Create snapshot {i + 1}/{num_snapshots}")
            self.container.create_snap()
            self.snap_epochs.append(self.container.epoch)
            self.log.info(
                "Snapshot %s captured at epoch %s", i + 1, self.snap_epochs[-1])

        self.log_step("Verify rank has objects before rebuild")
        self.verify_rank_has_objects()

        self.log_step(f"Exclude rank {rank} to trigger rebuild")
        self.start_rebuild()

        self.log_step("Wait for rebuild to complete")
        self.pool.wait_for_rebuild_to_end(interval=1)

        self.log_step("Restore container status to healthy")
        self.container.set_prop(prop="status", value="healthy")

        self.log_step("Verify all snapshot epochs remain readable after rebuild")
        for idx, epoch in enumerate(self.snap_epochs):
            self.log.info("Verifying epoch %s (snapshot %s)", epoch, idx + 1)
            self.verify_container_data(txn=epoch)

        self.log_step("Verify current data is also accessible after rebuild")
        self.verify_container_data()

        self.log_step("Verify pool info after rebuild")
        self.update_pool_verify()
        self.execute_pool_verify(" after rebuild with multiple epochs")

        self.log_step("Test Passed")

    def test_snapshot_during_rebuild(self):
        """JIRA ID: DAOS-18696.

        Test Description:
            Write objects, start rebuild by excluding a rank, then create a
            snapshot while rebuild is in progress. Verify that the snapshot
            epoch reflects consistent data and that the data at that epoch
            is accessible once rebuild completes.

        Use Cases:
            Snapshot creation during active rebuild captures consistent epoch.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,epoch,snapshot,race
        :avocado: tags=RbldEpoch,test_snapshot_during_rebuild
        """
        obj_class = self.params.get("object_class", "/run/rebuild/*")
        rank = self.params.get("rank", "/run/rebuild/*")

        self.log_step("Setup pool and container")
        self.setup_test_pool()
        self.setup_test_container()
        self.create_test_pool()

        self.log_step("Create container and write objects")
        self.container.create()
        self.container.write_objects(rank, obj_class)

        self.log_step("Verify rank has objects before rebuild")
        self.verify_rank_has_objects()

        self.log_step(f"Exclude rank {rank} to trigger rebuild")
        self.start_rebuild()

        self.log_step("Create snapshot during rebuild")
        self.container.set_prop(prop="status", value="healthy")
        self.container.create_snap()
        snap_epoch_during = self.container.epoch
        self.assertIsNotNone(
            snap_epoch_during, "Snapshot creation during rebuild should succeed")
        self.log.info("Snapshot captured at epoch %s during rebuild", snap_epoch_during)

        self.log_step("Wait for rebuild to complete")
        self.pool.wait_for_rebuild_to_end(interval=1)

        self.log_step("Verify data at snapshot epoch created during rebuild")
        self.verify_container_data(txn=snap_epoch_during)

        self.log_step("Verify current data is also accessible")
        self.verify_container_data()

        self.log_step("Verify pool info after rebuild")
        self.update_pool_verify()
        self.execute_pool_verify(" after rebuild with mid-rebuild snapshot")

        self.log_step("Test Passed")

    def test_epoch_read_during_rebuild(self):
        """JIRA ID: DAOS-18696.

        Test Description:
            Write objects and create a snapshot epoch, then trigger rebuild.
            While rebuild is in progress, read the data at the snapshot epoch.
            Verify that epoch-based reads succeed during the rebuild window,
            and that data integrity is maintained after rebuild completes.

        Use Cases:
            Epoch-pinned reads remain available and correct during rebuild.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,epoch,snapshot
        :avocado: tags=RbldEpoch,test_epoch_read_during_rebuild
        """
        obj_class = self.params.get("object_class", "/run/rebuild/*")
        rank = self.params.get("rank", "/run/rebuild/*")

        self.log_step("Setup pool and container")
        self.setup_test_pool()
        self.setup_test_container()
        self.create_test_pool()

        self.log_step("Create container and write objects")
        self.container.create()
        self.container.write_objects(rank, obj_class)

        self.log_step("Create snapshot to pin current epoch")
        self.container.create_snap()
        snap_epoch = self.container.epoch
        self.assertIsNotNone(snap_epoch, "Snapshot epoch must be set")
        self.log.info("Pinned snapshot at epoch %s", snap_epoch)

        self.log_step("Verify rank has objects before rebuild")
        self.verify_rank_has_objects()

        self.log_step(f"Exclude rank {rank} to trigger rebuild")
        self.start_rebuild()

        self.log_step("Read at pinned epoch during active rebuild")
        self.container.set_prop(prop="status", value="healthy")
        self.verify_container_data(txn=snap_epoch)

        self.log_step("Wait for rebuild to complete")
        self.pool.wait_for_rebuild_to_end(interval=1)

        self.log_step("Verify data at pinned epoch is still correct after rebuild")
        self.verify_container_data(txn=snap_epoch)

        self.log_step("Verify current data is also correct after rebuild")
        self.verify_container_data()

        self.log_step("Verify pool info after rebuild")
        self.update_pool_verify()
        self.execute_pool_verify(" after rebuild with epoch read during rebuild")

        self.log_step("Test Passed")

    def test_rebuild_snapshot_epoch_then_destroy(self):
        """JIRA ID: DAOS-18696.

        Test Description:
            Write objects, create a snapshot, trigger rebuild, verify the
            snapshot epoch is accessible after rebuild, then destroy the
            snapshot and verify only current data remains accessible.

        Use Cases:
            Snapshot lifecycle (create → rebuild → destroy) preserves data integrity.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,epoch,snapshot
        :avocado: tags=RbldEpoch,test_rebuild_snapshot_epoch_then_destroy
        """
        obj_class = self.params.get("object_class", "/run/rebuild/*")
        rank = self.params.get("rank", "/run/rebuild/*")

        self.log_step("Setup pool and container")
        self.setup_test_pool()
        self.setup_test_container()
        self.create_test_pool()

        self.log_step("Create container and write objects")
        self.container.create()
        self.container.write_objects(rank, obj_class)

        self.log_step("Create snapshot")
        self.container.create_snap()
        snap_epoch = self.container.epoch
        self.assertIsNotNone(snap_epoch, "Snapshot epoch must be set after create_snap")
        self.log.info("Snapshot created at epoch %s", snap_epoch)

        self.log_step("Write additional objects after snapshot")
        self.container.write_objects(obj_class=obj_class)

        self.log_step("Verify rank has objects before rebuild")
        self.verify_rank_has_objects()

        self.log_step(f"Exclude rank {rank} to trigger rebuild")
        self.start_rebuild()

        self.log_step("Wait for rebuild to complete")
        self.pool.wait_for_rebuild_to_end(interval=1)

        self.log_step("Restore container status to healthy")
        self.container.set_prop(prop="status", value="healthy")

        self.log_step("Verify snapshot epoch data is accessible after rebuild")
        self.verify_container_data(txn=snap_epoch)

        self.log_step("Destroy snapshot")
        self.container.destroy_snap(epcrange=snap_epoch)
        self.assertIsNone(
            self.container.epoch, "container.epoch should be None after destroy_snap")

        self.log_step("Verify current data is still accessible after snapshot destroyed")
        self.verify_container_data()

        self.log_step("Verify pool info after rebuild")
        self.update_pool_verify()
        self.execute_pool_verify(" after rebuild and snapshot destroy")

        self.log_step("Test Passed")

