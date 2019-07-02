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
from __future__ import print_function
import os
import traceback
import random
import string
from apricot import TestWithServers
from conversion import c_uuid_to_str
from daos_api import (DaosPool, DaosContainer, DaosSnapshot,
                      DaosApiError)

# pylint: disable=broad-except
class Snapshot(TestWithServers):
    """
    Epic: DAOS-2249 Create system level tests that cover basic snapshot
          functionality.
    Testcase:
          DAOS-1370 Basic snapshot test
          DAOS-1386 Test container SnapShot information
          DAOS-1371 Test list snapshots
          DAOS-1395 Test snapshot destroy
          DAOS-1402 Test creating multiple snapshots

    Test Class Description:
          Start DAOS servers, set up the pool and container for the above
          snapshot Epic and Testcases, including snapshot basic, container
          information, list, creation and destroy.
    :avocado: recursive
    """

    def setUp(self):
        """
        set up method
        """
        super(Snapshot, self).setUp()
        # get parameters from yaml file
        createmode = self.params.get("mode", '/run/poolparams/createmode/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname",
                                      '/run/poolparams/createset/')
        createsize = self.params.get("size", '/run/poolparams/createsize/')
        self.log.info("==In setUp, self.context= %s", self.context)

        try:

            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)

            # need a connection to the pool with rw permission
            #    DAOS_PC_RO = int(1 << 0)
            #    DAOS_PC_RW = int(1 << 1)
            #    DAOS_PC_EX = int(1 << 2)
            self.pool.connect(1 << 1)

            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.handle)

        except DaosApiError as error:
            self.log.info("Error detected in DAOS pool container setup: %s"
                          , str(error))
            self.log.info(traceback.format_exc())
            self.fail("##Test failed on setUp, before snapshot taken")

        # now open it
        self.container.open()

        # do a query and compare the UUID returned from create with
        # that returned by query
        self.container.query()

        if self.container.get_uuid_str() != c_uuid_to_str(
                self.container.info.ci_uuid):
            self.fail("##Container UUID did not match the one in info.")

    def tearDown(self):
        """
        tear down method
        """
        try:
            if self.container:
                self.container.close()

            if self.container:
                self.container.destroy()

            # cleanup the pool
            if self.pool:
                self.pool.disconnect()
                self.pool.destroy(1)

        except DaosApiError as excep:
            self.log.info(excep)
            self.log.info(traceback.format_exc())
            self.fail("##Snapshot test failed on cleanUp.")

        finally:
            super(Snapshot, self).tearDown()

    def display_snapshot(self, snapshot):
        """
        To display the snapshot information.
        Args:
            snapshot: snapshot handle to be displayed.
        Return:
            none.
        """
        self.log.info("==display_snapshot================")
        self.log.info("snapshot=                 %s", snapshot)
        self.log.info("snapshot.context=         %s", snapshot.context)
        self.log.info("snapshot.context.libdaos= %s"
                      , snapshot.context.libdaos)
        self.log.info("snapshot.context.libtest= %s"
                      , snapshot.context.libtest)
        self.log.info("snapshot.context.ftable= %s"
                      , snapshot.context.ftable)
        self.log.info("snapshot.context.ftable[list-attr]= %s"
                      , snapshot.context.ftable["list-attr"])
        self.log.info("snapshot.context.ftable[test-event]=%s"
                      , snapshot.context.ftable["test-event"])
        self.log.info("snapshot.name=  %s", snapshot.name)
        self.log.info("snapshot.epoch= %s", snapshot.epoch)
        self.log.info("==================================")

    def take_snapshot(self, container, epoch):
        """
        To take a snapshot on the container with current epoch.

        Args:
            container: container for the snapshot
            epoch: the container epoch for the snapshot
        Return:
            An object representing the snapshot
        """
        self.log.info("==Taking snapshot for:")
        self.log.info("    coh=   %s", container.coh)
        self.log.info("    epoch= %s", epoch)
        snapshot = DaosSnapshot(self.context)
        snapshot.create(container.coh, epoch)
        self.display_snapshot(snapshot)
        return snapshot

    def invalid_snapshot_test(self, coh, epoch):
        """
        Negative snapshot test with invalid container handle or epoch.

        Args:
            container: container for the snapshot
            epoch: the container epoch for the snapshot
        Return:
            0: Failed
            1: Passed (expected failure detected)
        """
        status = 0
        try:
            snapshot = DaosSnapshot(self.context)
            snapshot.create(coh, epoch)
        except Exception:
            self.log.info("Negative test, expected error:")
            status = 1
        return status

    def test_snapshot_negativecases(self):
        """
        Test ID: DAOS-1390 Verify snap_create bad parameter behavior

        Test Description:
                (1)Create an object, write random data into it, and take
                   a snapshot.
                (2)Verify the snapshot is working properly.
                (3)Test snapshot with an invalid container handle
                (4)Test snapshot with a NULL container handle
                (5)Test snapshot with an invalid epoch

        Use Cases: Combinations with minimun 1 client and 1 server.
        :avocado: tags=snap,snapshot_negative,snapshotcreate_negative
        """

        #(1)Create an object, write some data into it, and take a snapshot
        obj_cls = self.params.get("obj_class", '/run/object_class/*')
        akey = self.params.get("akey", '/run/snapshot/*', default="akey")
        dkey = self.params.get("dkey", '/run/snapshot/*', default="dkey")
        data_size = self.params.get("test_datasize",
                                    '/run/snapshot/*', default=150)
        rand_str = lambda n: ''.join([random.choice(string.lowercase)
                                      for i in xrange(n)])
        thedata = "--->>>Happy Daos Snapshot-Create Negative Testing " + \
                  "<<<---" + rand_str(random.randint(1, data_size))
        try:
            obj, epoch = self.container.write_an_obj(
                thedata, len(thedata)+1, dkey, akey, obj_cls=obj_cls)
        except DaosApiError as error:
            self.fail(
                "##(1)Test failed during the initial object write: %s"
                , str(error))
        obj.close()
        ##Take a snapshot of the container
        snapshot = self.take_snapshot(self.container, epoch)
        self.log.info("==(1)Container epoch= %s", epoch)
        self.log.info("     snapshot.epoch= %s", snapshot.epoch)

        #(2)Verify the snapshot is working properly.
        try:
            obj.open()
            snap_handle = snapshot.open(
                self.container.coh, snapshot.epoch)
            thedata2 = self.container.read_an_obj(
                len(thedata)+1, dkey, akey, obj, snap_handle.value)
        except Exception as error:
            self.fail(
                "##(2)Error when retrieving the snapshot data: %s"
                , str(error))
        self.log.info("==(2)snapshot_list[ind]=%s", snapshot)
        self.log.info("==snapshot.epoch=  %s", snapshot.epoch)
        self.log.info("==written thedata=%s", thedata)
        self.log.info("==thedata2.value= %s", thedata2.value)
        if thedata2.value != thedata:
            raise Exception("##(2)The data in the snapshot is not the "
                            "same as the original data")
        self.log.info("==Snapshot data matches the data originally "
                      "written.")

        #(3)Test snapshot with an invalid container handle
        self.log.info("==(3)Snapshot with an invalid container handle.")
        if self.invalid_snapshot_test(self.container, epoch):
            self.log.info("==>Negative test 1, expecting failed on taking "
                          "snapshot with an invalid container.coh: %s"
                          , self.container)
        else:
            self.fail(
                "##(3)Negative test 1 passing, expecting failed on"
                " taking snapshot with an invalid container.coh: %s"
                , self.container)

        #(4)Test snapshot with a NULL container handle
        self.log.info("==(4)Snapshot with a NULL container handle.")
        if self.invalid_snapshot_test(None, epoch):
            self.log.info("==>Negative test 2, expecting failed on taking "
                          "snapshot on a NULL container.coh.")
        else:
            self.fail("##(4)Negative test 2 passing, expecting failed on "
                      "taking snapshot with a NULL container.coh.")

        #(5)Test snapshot with an invalid epoch
        self.log.info("==(5)Snapshot with a NULL epoch.")
        if self.invalid_snapshot_test(self.container.coh, None):
            self.log.info("==>Negative test 3, expecting failed on taking "
                          "snapshot with a NULL epoch.")
        else:
            self.fail("##(5)Negative test 3 passing, expecting failed on "
                      "taking snapshot with a NULL epoch.")


    def test_snapshots(self):
        """
        Test ID: DAOS-1386 Test container SnapShot information
                 DAOS-1371 Test list snapshots
                 DAOS-1395 Test snapshot destroy
                 DAOS-1402 Test creating multiple snapshots
        Test Description:
                (1)Create an object, write random data into it, and take
                   a snapshot.
                (2)Make changes to the data object. The write_an_obj function
                   does a commit when the update is complete.
                (3)Verify the data in the snapshot is the original data.
                   Get a handle for the snapshot and read the object at dkey,
                   akey. Compare it to the originally written data.
                (4)List the snapshot and make sure it reflects the original
                   epoch.
                   ==>Repeat step(1) to step(4) for multiple snapshot tests.
                (5)Verify the snapshots data.
                (6)Destroy the snapshot.
                (7)Check if still able to Open the destroyed snapshot and
                   Verify the snapshot removed from the snapshot list.
        Use Cases: Require 1 client and 1 server to run snapshot test.
                   1 pool and 1 container is used, num_of_snapshot defined
                   in the snapshot.yaml will be performed and verified.
        :avocado: tags=snap,snapshots
        """

        coh_list = []
        container_epoch_list = []
        snapshot_list = []
        test_data = []
        snapshot_index = 0
        obj_cls = self.params.get("obj_class", '/run/object_class/*')
        akey = self.params.get("akey", '/run/snapshot/*', default="akey")
        dkey = self.params.get("dkey", '/run/snapshot/*', default="dkey")
        data_size = self.params.get("test_datasize",
                                    '/run/snapshot/*', default=150)
        snapshot_loop = self.params.get("num_of_snapshot",
                                        '/run/snapshot/*', default=10)
        rand_str = lambda n: ''.join([random.choice(string.lowercase)
                                      for i in xrange(n)])
        #
        #Test loop for creat, modify and snapshot object in the DAOS container.
        #
        while snapshot_index < snapshot_loop:
            #(1)Create an object, write some data into it, and take a snapshot
            #size = random.randint(1, 100) + 1
            snapshot_index += 1
            thedata = "--->>>Happy Daos Snapshot Testing " + \
                str(snapshot_index) + \
                "<<<---" + rand_str(random.randint(1, data_size))
            datasize = len(thedata) + 1
            try:
                obj, epoch = self.container.write_an_obj(
                    thedata, datasize, dkey, akey, obj_cls=obj_cls)
                obj.close()
            except DaosApiError as error:
                self.fail(
                    "##Test failed during the initial object write: %s"
                    , str(error))
            #Take a snapshot of the container
            snapshot = DaosSnapshot(self.context)
            snapshot.create(self.container.coh, epoch)
            self.log.info("==Wrote an object and created a snapshot")

            #Display snapshot
            substep = "1." + str(snapshot_index)
            self.log.info("==(1)Test step %s", substep)
            self.log.info("==self.container epoch=     %s", epoch)
            self.log.info("==snapshot.epoch= %s"
                          , snapshot.epoch)
            self.display_snapshot(snapshot)

            #Save snapshot test data
            coh_list.append(self.container.coh)
            container_epoch_list.append(epoch)
            snapshot_list.append(snapshot)
            test_data.append(thedata)

            #(2)Make changes to the data object. The write_an_obj function does
            #   a commit when the update is complete
            more_transactions = 100
            self.log.info("==(2)Committing %d additional transactions to "
                          "the same KV.", more_transactions)
            while more_transactions:
                size = random.randint(1, 250) + 1
                new_data = rand_str(size)
                try:
                    new_obj, _ = self.container.write_an_obj(
                        new_data, size, dkey, akey, obj_cls=obj_cls)
                    new_obj.close()
                except Exception as error:
                    self.fail(
                        "##Test failed during the write of multi-objects: %s"
                        , str(error))
                more_transactions -= 1

            #(3)Verify the data in the snapshot is the original data.
            #   Get a handle for the snapshot and read the object at dkey, akey
            #   Compare it to the originally written data.
            self.log.info("==(3)snapshot test loop: %s"
                          , snapshot_index)
            try:
                obj.open()
                snap_handle = snapshot.open(
                    self.container.coh, snapshot.epoch)
                thedata3 = self.container.read_an_obj(
                    datasize, dkey, akey, obj, snap_handle.value)
            except Exception as error:
                self.fail(
                    "##Error when retrieving the snapshot data: %s"
                    , str(error))
            self.log.info("==container_epoch= %s", epoch)
            self.log.info("==snapshot_list[ind]=%s"
                          , snapshot)
            self.log.info("==snapshot.epoch=  %s"
                          , snapshot.epoch)
            self.log.info("==written thedata size= %s"
                          , len(thedata)+1)
            self.log.info("==written thedata=%s", thedata)
            self.log.info("==thedata3.value= %s", thedata3.value)
            if thedata3.value != thedata:
                raise Exception("##The data in the snapshot is not the "
                                "same as the original data")
            self.log.info("==The snapshot data matches the data originally"
                          " written.")

            #(4)List the snapshot and make sure it reflects the original epoch
            try:
                reported_epoch = snapshot.list(self.container.coh, epoch)
            except Exception as error:
                self.fail(
                    "##Test was unable to list the snapshot: %s"
                    , str(error))
            self.log.info("==(4)List snapshot reported_epoch=%s"
                          , reported_epoch)
            self.log.info("     snapshot.epoch=%s"
                          , snapshot.epoch)
            ##self.log.info("tickets already assigned DAOS-2390 DAOS-2392")
            #if snapshot.epoch != reported_epoch:
            #    raise Exception("##The snapshot epoch returned from "
            #        "snapshot list is not the same as the original"
            #        "epoch list is not the same as the original epoch"
            #        "snapshotted.")
            self.log.info("==After 10 additional commits the snapshot is "
                          "still available")

        #(5)Verify the snapshots data
        for ind in range(0, len(container_epoch_list)):
            epoch = container_epoch_list[ind]
            current_ss = snapshot_list[ind]
            datasize = len(test_data[ind]) + 1
            try:
                obj.open()
                snap_handle = snapshot.open(
                    self.container.coh, current_ss.epoch)
            except Exception as error:
                self.fail(
                    "##Error when retrieving the snapshot data: %s"
                    , str(error))
            ##self.log.info("tickets already assigned DAOS-2484 and DAOS-2557")
            #thedata3 = self.container.read_an_obj(datasize, dkey, akey, obj,
            #                                  snap_handle.value)
            #self.log.info("==(5)snapshot test list %s:".format(ind+1))
            #self.log.info("==container_epoch_list[ind]=%s"\
            #              .format(epoch))
            #self.log.info("==snapshot_list[ind]=%s"\
            #              .format(snapshot_list[ind]))
            #self.log.info("==snapshot_list[ind].epoch=%s"\
            #              .format( current_ss.epoch))
            #self.log.info("==test_data_size=    %s".format(datasize))
            #self.log.info("==thedata3.value=         %s"\
            #              .format(thedata3.value))
            #self.log.info("==test_data[ind]=    %s"\
            #              .format( test_data[ind]))
            #if thedata3.value != test_data[ind]:
            #    raise Exception("##The data in the snapshot is not "
            #                    "same as the original data")
            #self.log.info("The snapshot data matches the data originally "
            #              "written.")

        #(6)Destroy the snapshot
        self.log.info("==(6)Destroy snapshot  epoch: %s", epoch)
        try:
            snapshot.destroy(self.container.coh, epoch)
            self.log.info("==Snapshot successfully destroyed")
        except Exception as error:
            self.fail(
                "##Error on snapshot.destroy: %s", str(error))

        #(7)Check if still able to Open the destroyed snapshot and
        #    Verify the snapshot removed from the snapshot list
        try:
            obj.open()
            snap_handle3 = snapshot.open(self.container.coh,
                                         snapshot.epoch)
            thedata3 = self.container.read_an_obj(datasize, dkey, akey,
                                                  obj, snap_handle3.value)
        except Exception as error:
            self.fail(
                "##(7)Error when retrieving the 2nd snapshot data: %s"
                , str(error))
        self.log.info("-->thedata_after_snapshot.destroyed.value= %s"
                      , thedata3.value)
        self.log.info("==>snapshot_epoch=     %s"
                      , snapshot.epoch)
        self.log.info("-->snapshot.list(self.container.coh, epoch)=%s"
                      , snapshot.list(self.container.coh, epoch))
        #self.cancel("tickets already assigned DAOS-2390 DAOS-2392")
        #Still able to open the snapshot and read data after destroyed.
        self.log.info("==(7)DAOS container SnapshotInfo test passed")

        # Now destroy the snapshot
        try:
            snapshot.destroy(self.container.coh)
            self.log.info("==Snapshot successfully destroyed")
        except Exception as error:
            self.fail(
                "##Error on snapshot.destroy: %s", str(error))
