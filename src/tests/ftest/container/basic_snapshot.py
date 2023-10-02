"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from pydaos.raw import DaosContainer, DaosSnapshot, DaosApiError

from apricot import TestWithServers
from general_utils import get_random_bytes


class BasicSnapshot(TestWithServers):
    """DAOS-1370 Basic snapshot test.

    Test Class Description:
        Test that a snapshot taken of a container remains unchanged even after
        an object in the container has been updated 500 times.
        Create the container.
        Write an object to the container.
        Take a snapshot.
        Write 500 changes to the KV pair of the object.
        Check that the snapshot is still there.
        Confirm that the data in the snapshot is unchanged.
        Destroy the snapshot

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a BasicSnapshot object."""
        super().__init__(*args, **kwargs)
        self.snapshot = None

    def test_basic_snapshot(self):
        """Test ID: DAOS-1370.

        Test Description:
            Create a pool, container in the pool, object in the container, add
            one key:value to the object.
            Commit the transaction. Perform a snapshot create on the container.
            Create 500 additional transactions with a small change to the object
            in each and commit each after the object update is done.
            Verify the snapshot is still available and the contents remain in
            their original state.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=container,snap,snapshot
        :avocado: tags=BasicSnapshot,test_basic_snapshot
        """
        # Set up the pool and container.
        try:
            # initialize a pool object then create the underlying
            # daos storage, and connect
            self.prepare_pool()

            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.pool.handle)

            # now open it
            self.container.open()

        except DaosApiError as error:
            self.log.error(str(error))
            self.fail("Test failed before snapshot taken")

        try:
            # create an object and write some data into it
            obj_cls = self.params.get("obj_class", '/run/object_class/*')
            thedata = b"Now is the winter of our discontent made glorious"
            datasize = len(thedata) + 1
            dkey = b"dkey"
            akey = b"akey"
            obj = self.container.write_an_obj(thedata,
                                              datasize,
                                              dkey,
                                              akey,
                                              obj_cls=obj_cls)
            obj.close()
            # Take a snapshot of the container
            self.snapshot = DaosSnapshot(self.context)
            self.snapshot.create(self.container.coh)
            self.log.info("Wrote an object and created a snapshot")

        except DaosApiError as error:
            self.fail("Test failed during the initial object write.\n{0}"
                      .format(error))

        # Make 500 changes to the data object. The write_an_obj function does a
        # commit when the update is complete
        try:
            self.log.info(
                "Committing 500 additional transactions to the same KV")
            more_transactions = 500
            while more_transactions:
                size = self.random.randint(1, 250) + 1
                new_data = get_random_bytes(size)
                new_obj = self.container.write_an_obj(
                    new_data, size, dkey, akey, obj_cls=obj_cls)
                new_obj.close()
                more_transactions -= 1
        except DaosApiError as error:
            self.fail(
                "Test failed during the write of 500 objects.\n{0}".format(
                    error))

        # List the snapshot
        try:
            reported_epoch = self.snapshot.list(self.container.coh)
        except DaosApiError as error:
            self.fail(
                "Test was unable to list the snapshot\n{0}".format(error))

        # Make sure the snapshot reflects the original epoch
        if self.snapshot.epoch != reported_epoch:
            self.fail(
                "The snapshot epoch returned from snapshot list is not the "
                "same as the original epoch snapshotted.")

        self.log.info(
            "After 500 additional commits the snapshot is still available")

        # Make sure the data in the snapshot is the original data.
        # Get a handle for the snapshot and read the object at dkey, akey.
        try:
            obj.open()
            snap_handle = self.snapshot.open(self.container.coh)
            thedata2 = self.container.read_an_obj(
                datasize, dkey, akey, obj, txn=snap_handle.value)
        except DaosApiError as error:
            self.fail(
                "Error when retrieving the snapshot data.\n{0}".format(error))

        # Compare the snapshot to the originally written data.
        if thedata2.value != thedata:
            self.fail(
                "The data in the snapshot is not the same as the original data")

        self.log.info(
            "The snapshot data matches the data originally written.")

        # Now destroy the snapshot
        try:
            self.snapshot.destroy(self.container.coh)
            self.log.info("Snapshot successfully destroyed")

        except DaosApiError as error:
            self.fail(str(error))
