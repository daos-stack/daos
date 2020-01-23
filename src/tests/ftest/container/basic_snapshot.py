#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

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
import os
import random

from apricot import TestWithServers, skipForTicket
from pydaos.raw import DaosPool, DaosContainer, DaosSnapshot, DaosApiError
from general_utils import get_random_string


class BasicSnapshot(TestWithServers):
    """DAOS-1370 Basic snapshot test.

    Test Class Description:
        Test that a snapshot taken of a container remains unchaged even after
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
        super(BasicSnapshot, self).__init__(*args, **kwargs)
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

        :avocado: tags=snap,basicsnap
        """
        # Set up the pool and container.
        try:
            # parameters used in pool create
            createmode = self.params.get("mode", '/run/pool/createmode/')
            createsetid = self.params.get("setname", '/run/pool/createset/')
            createsize = self.params.get("size", '/run/pool/createsize/*')
            createuid = os.geteuid()
            creategid = os.getegid()

            # initialize a pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)

            # need a connection to create container
            self.pool.connect(1 << 1)

            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.handle)

            # now open it
            self.container.open()

        except DaosApiError as error:
            self.log.error(str(error))
            self.fail("Test failed before snapshot taken")

        try:
            # create an object and write some data into it
            obj_cls = self.params.get("obj_class", '/run/object_class/*')
            thedata = "Now is the winter of our discontent made glorious"
            datasize = len(thedata) + 1
            dkey = "dkey"
            akey = "akey"
            tx_handle = self.container.get_new_tx()
            obj = self.container.write_an_obj(thedata,
                                              datasize,
                                              dkey,
                                              akey,
                                              obj_cls=obj_cls,
                                              txn=tx_handle)
            self.container.commit_tx(tx_handle)
            obj.close()
            # Take a snapshot of the container
            self.snapshot = DaosSnapshot(self.context)
            self.snapshot.create(self.container.coh, tx_handle)
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
                size = random.randint(1, 250) + 1
                new_data = get_random_string(size)
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
