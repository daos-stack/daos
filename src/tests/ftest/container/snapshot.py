"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import traceback

from pydaos.raw import DaosContainer, DaosSnapshot, DaosApiError, c_uuid_to_str

from apricot import TestWithServers
from general_utils import get_random_bytes


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
        super().setUp()
        self.log.info("==In setUp, self.context= %s", self.context)

        # initialize a python pool object then create the underlying
        # daos storage and connect to it
        self.prepare_pool()

        try:
            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.pool.handle)

        except DaosApiError as error:
            self.log.info("Error detected in DAOS pool container setup: %s", str(error))
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
        self.log.info("snapshot.context.libdaos= %s", snapshot.context.libdaos)
        self.log.info("snapshot.context.libtest= %s", snapshot.context.libtest)
        self.log.info("snapshot.context.ftable= %s", snapshot.context.ftable)
        self.log.info("snapshot.context.ftable[list-attr]= %s",
                      snapshot.context.ftable["list-attr"])
        self.log.info("snapshot.context.ftable[test-event]=%s",
                      snapshot.context.ftable["test-event"])
        self.log.info("snapshot.name=  %s", snapshot.name)
        self.log.info("snapshot.epoch= %s", snapshot.epoch)
        self.log.info("==================================")

    def take_snapshot(self, container):
        """
        To take a snapshot on the container on current epoch.

        Args:
            container: container for the snapshot
        Return:
            An object representing the snapshot
        """
        self.log.info("==Taking snapshot for:")
        self.log.info("    coh=   %s", container.coh)
        snapshot = DaosSnapshot(self.context)
        snapshot.create(container.coh)
        self.display_snapshot(snapshot)
        return snapshot

    def invalid_snapshot_test(self, coh):
        """
        Negative snapshot test with invalid container handle.

        Args:
            container: container for the snapshot
        Return:
            0: Failed
            1: Passed (expected failure detected)
        """
        status = 0
        try:
            snapshot = DaosSnapshot(self.context)
            snapshot.create(coh)
        except Exception as error:
            self.log.info("==>Negative test, expected error: %s", str(error))
            status = 1
        return status

    def test_snapshot_negativecases(self):
        # pylint: disable=no-member
        """
        Test ID: DAOS-1390 Verify snap_create bad parameter behavior.
                 DAOS-1322 Create a new container, verify snapshot state.
                           as expected for a brand new container.
                 DAOS-1392 Verify snap_destroy bad parameter behavior.
                 DAOS-1388 Verify snap_list bad parameter behavior.
        Test Description:
                (0)Take a snapshot of the newly created container.
                (1)Create an object, write random data into it, and take
                   a snapshot.
                (2)Verify the snapshot is working properly.
                (3)Test snapshot with an invalid container handle.
                (4)Test snapshot with a NULL container handle.
                (5)Verify snap_destroy with a bad parameter.
                (6)Verify snap_list bad parameter behavior.

        Use Cases: Combinations with minimum 1 client and 1 server.
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=container,smoke,snap,snapshot
        :avocado: tags=snapshot_negative,snapshotcreate_negative,test_snapshot_negativecases
        """

        # DAOS-1322 Create a new container, verify snapshot state as expected
        #           for a brand new container.
        try:
            self.log.info(
                "==(0)Take a snapshot of the newly created container.")
            snapshot = DaosSnapshot(self.context)
            snapshot.create(self.container.coh)
            self.display_snapshot(snapshot)
        except Exception as error:
            self.fail("##(0)Error on a snapshot on a new container"
                      " {}".format(str(error)))

        # (1)Create an object, write some data into it, and take a snapshot
        obj_cls = self.params.get("obj_class", '/run/object_class/*')
        akey = self.params.get("akey", '/run/snapshot/*', default="akey")
        dkey = self.params.get("dkey", '/run/snapshot/*', default="dkey")
        akey = akey.encode("utf-8")
        dkey = dkey.encode("utf-8")
        data_size = self.params.get("test_datasize",
                                    '/run/snapshot/*', default=150)
        thedata = b"--->>>Happy Daos Snapshot-Create Negative Testing " + \
                  b"<<<---" + get_random_bytes(self.random.randint(1, data_size))
        try:
            obj = self.container.write_an_obj(thedata,
                                              len(thedata) + 1,
                                              dkey,
                                              akey,
                                              obj_cls=obj_cls)
        except DaosApiError as error:
            self.fail(
                "##(1)Test failed during the initial object write:"
                " {}".format(str(error)))
        obj.close()
        # Take a snapshot of the container
        snapshot = self.take_snapshot(self.container)
        self.log.info("==(1)snapshot.epoch= %s", snapshot.epoch)

        # (2)Verify the snapshot is working properly.
        try:
            obj.open()
            snap_handle = snapshot.open(self.container.coh, snapshot.epoch)
            thedata2 = self.container.read_an_obj(
                len(thedata) + 1, dkey, akey, obj, txn=snap_handle.value)
        except Exception as error:
            self.fail(
                "##(2)Error when retrieving the snapshot data:"
                " {}".format(str(error)))
        self.log.info("==(2)snapshot_list[ind]=%s", snapshot)
        self.log.info("==snapshot.epoch=  %s", snapshot.epoch)
        self.log.info("==written thedata[:200]=%s", thedata[:200])
        self.log.info("==thedata2.value[:200] =%s", thedata2.value[:200])
        if thedata2.value != thedata:
            self.fail("##(2)The data in the snapshot is not the same as the original data")
        self.log.info("==Snapshot data matches the data originally written.")

        # (3)Test snapshot with an invalid container handle
        self.log.info("==(3)Snapshot with an invalid container handle.")
        if self.invalid_snapshot_test(self.container):
            self.log.info("==>Negative test 1, expecting failed on taking "
                          "snapshot with an invalid container.coh: %s", self.container)
        else:
            self.fail(
                "##(3)Negative test 1 passing, expecting failed on"
                " taking snapshot with an invalid container.coh: "
                " {}".format(self.container))

        # (4)Test snapshot with a NULL container handle
        self.log.info("==(4)Snapshot with a NULL container handle.")
        if self.invalid_snapshot_test(None):
            self.log.info("==>Negative test 2, expecting failed on taking "
                          "snapshot on a NULL container.coh.")
        else:
            self.fail("##(4)Negative test 2 passing, expecting failed on "
                      "taking snapshot with a NULL container.coh.")

        # (5)DAOS-1392 destroy snapshot with an invalid handle
        self.log.info(
            "==(6)DAOS-1392 destroy snapshot with an invalid handle.")
        try:
            snapshot.destroy(None, snapshot.epoch)
            self.fail(
                "##(6)Negative test destroy snapshot with an "
                "invalid coh handle, expected fail, shown Passing##")
        except Exception as error:
            self.log.info(
                "==>Negative test, destroy snapshot with an invalid handle.")
            self.log.info("   Expected Error: %s", str(error))
            expected_error = "RC: -1002"
            if expected_error not in str(error):
                self.fail(
                    "##(6.1)Expecting error RC: -1002  did not show.")

        # (6)DAOS-1388 Verify snap_list bad parameter behavior
        self.log.info(
            "==(7)DAOS-1388 Verify snap_list bad parameter behavior.")
        try:
            snapshot.list(None, 0)
            self.fail(
                "##(7)Negative test snapshot list with an "
                "invalid coh and epoch, expected fail, shown Passing##")
        except Exception as error:
            self.log.info(
                "==>Negative test, snapshot list with an invalid coh.")
            self.log.info("   Expected Error: %s", str(error))
            expected_error = "RC: -1002"
            if expected_error not in str(error):
                self.fail(
                    "##(7.1)Expecting error RC: -1002  did not show.")

    def display_snapshot_test_data(self, test_data, ss_index):
        """Display the snapshot test data.

        Args:
            test_data: list of snapshot testdata
                dictionary keys:
                    coh:             container handle
                    snapshot:        snapshot handle
                    tst_obj:         test object
                    tst_data:        test data
            ss_index: snapshot-list index to be displayed.
        """
        if len(test_data) < ss_index - 1:
            self.log.info("##Under to display test_data info, "
                          "index out of range.")
        else:
            ind = ss_index - 1
            self.log.info("  =Snapshot number : %s",
                          ss_index)
            self.log.info("  ==container_coh     =%s",
                          test_data[ind]["coh"])
            self.log.info("  ==snapshot          =%s",
                          test_data[ind]["snapshot"])
            self.log.info("  ==snapshot.epoch    =%s",
                          test_data[ind]["snapshot"].epoch)
            self.log.info("  ==data obj          =%s",
                          test_data[ind]["tst_obj"])
            self.log.info("  ==snapshot tst_data_size= %s",
                          len(test_data[ind]["tst_data"]) + 1)
            self.log.info("  ==original tst_data[:200] =%s", test_data[ind]["tst_data"][:200])

    def test_snapshots(self):
        # pylint: disable=no-member,too-many-locals
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
                (6)Destroy the snapshot individually.
                   ==>Loop step(5) and step(6) to perform multiple snapshots
                   data verification and snapshot destroy test.
                (7)Check if still able to Open the destroyed snapshot and
                   Verify the snapshot removed from the snapshot list.
        Use Cases: Require 1 client and 1 server to run snapshot test.
                   1 pool and 1 container is used, num_of_snapshot defined
                   in the snapshot.yaml will be performed and verified.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container,smoke,snap,snapshot
        :avocado: tags=snapshots,test_snapshots
        """

        test_data = []
        ss_number = 0
        obj_cls = self.params.get("obj_class", '/run/object_class/*')
        akey = self.params.get("akey", '/run/snapshot/*', default="akey")
        dkey = self.params.get("dkey", '/run/snapshot/*', default="dkey")
        akey = akey.encode("utf-8")
        dkey = dkey.encode("utf-8")
        data_size = self.params.get("test_datasize",
                                    '/run/snapshot/*', default=150)
        snapshot_loop = self.params.get("num_of_snapshot",
                                        '/run/snapshot/*', default=3)
        #
        # Test loop for creat, modify and snapshot object in the DAOS container.
        #
        while ss_number < snapshot_loop:
            # (1)Create an object, write some data into it, and take a snapshot
            ss_number += 1
            thedata = b"--->>>Happy Daos Snapshot Testing " + \
                str(ss_number).encode("utf-8") + \
                b"<<<---" + get_random_bytes(self.random.randint(1, data_size))
            datasize = len(thedata) + 1
            try:
                obj = self.container.write_an_obj(thedata,
                                                  datasize,
                                                  dkey,
                                                  akey,
                                                  obj_cls=obj_cls)
                obj.close()
            except DaosApiError as error:
                self.fail("##(1)Test failed during the initial object "
                          "write: {}".format(str(error)))
            # Take a snapshot of the container
            snapshot = DaosSnapshot(self.context)
            snapshot.create(self.container.coh)
            self.log.info("==Wrote an object and created a snapshot")

            # Display snapshot
            self.log.info("=(1.%s)snapshot test loop: %s", ss_number, ss_number)
            self.log.info("  ==snapshot.epoch= %s", snapshot.epoch)
            self.display_snapshot(snapshot)

            # Save snapshot test data
            test_data.append(
                {"coh": self.container.coh,
                 "tst_obj": obj,
                 "snapshot": snapshot,
                 "tst_data": thedata})

            # (2)Make changes to the data object. The write_an_obj function does
            #    a commit when the update is complete
            num_transactions = more_transactions = 200
            self.log.info("=(2.%s)Committing %d additional transactions to "
                          "the same KV.", ss_number, more_transactions)
            while more_transactions:
                size = self.random.randint(1, 250) + 1
                new_data = get_random_bytes(size)
                try:
                    new_obj = self.container.write_an_obj(
                        new_data, size, dkey, akey, obj_cls=obj_cls)
                    new_obj.close()
                except Exception as error:
                    self.fail("##(2)Test failed during the write of "
                              "multi-objects: {}".format(str(error)))
                more_transactions -= 1

            # (3)Verify the data in the snapshot is the original data.
            #    Get a handle for the snapshot and read the object at dkey, akey
            #    Compare it to the originally written data.
            self.log.info("=(3.%s)snapshot test loop: %s", ss_number, ss_number)
            try:
                obj.open()
                snap_handle = snapshot.open(
                    self.container.coh, snapshot.epoch)
                thedata3 = self.container.read_an_obj(
                    datasize, dkey, akey, obj, txn=snap_handle.value)
                obj.close()
            except Exception as error:
                self.fail("##(3.1)Error when retrieving the snapshot data: {}"
                          .format(str(error)))
            self.display_snapshot_test_data(test_data, ss_number)
            self.log.info("  ==thedata3.value[:200]= %s", thedata3.value[:200])
            if thedata3.value != thedata:
                self.fail("##(3.2)The data in the snapshot is not the same as the original data")
            self.log.info("  ==The snapshot data matches the data originally written.")

            # (4)List the snapshot and make sure it reflects the original epoch
            try:
                ss_list = snapshot.list(self.container.coh, snapshot.epoch)
                self.log.info("=(4.%s)snapshot.list(self.container.coh)= %s",
                              ss_number, ss_list)
                self.log.info("  ==snapshot.epoch=  %s", snapshot.epoch)

            except Exception as error:
                self.fail("##(4)Test was unable to list the snapshot: {}"
                          .format(str(error)))
            self.log.info("  ==After %s additional commits the snapshot is "
                          "still available", num_transactions)

        # (5)Verify the snapshots data
        #    Step(5) and (6), test loop to perform multiple snapshots data
        #    verification and snapshot destroy.
        #    Use current_ss for the individual snapshot object.
        for ss_number in range(snapshot_loop - 1, 0, -1):
            ind = ss_number - 1
            self.log.info("=(5.%s)Verify the snapshot number %s:", ss_number, ss_number)
            self.display_snapshot_test_data(test_data, ss_number)
            coh = test_data[ind]["coh"]
            current_ss = test_data[ind]["snapshot"]
            obj = test_data[ind]["tst_obj"]
            tst_data = test_data[ind]["tst_data"]
            datasize = len(tst_data) + 1
            try:
                obj.open()
                snap_handle5 = current_ss.open(coh, current_ss.epoch)
                thedata5 = self.container.read_an_obj(
                    datasize, dkey, akey, obj, txn=snap_handle5.value)
                obj.close()
            except Exception as error:
                self.fail("##(5.1)Error when retrieving the snapshot data: {}"
                          .format(str(error)))
            self.log.info("  ==snapshot tst_data[:200] =%s", thedata5.value[:200])
            if thedata5.value != tst_data:
                self.fail("##(5.2)Snapshot #{}, test data Mis-matches"
                          "the original data written.".format(ss_number))
            self.log.info("  snapshot test number %s, test data matches"
                          " the original data written.", ss_number)

        # (6)Destroy the individual snapshot
            self.log.info("=(6.%s)Destroy the snapshot epoch: %s",
                          ss_number, current_ss.epoch)
            try:
                current_ss.destroy(coh, current_ss.epoch)
                self.log.info(
                    "  ==snapshot.epoch %s successfully destroyed",
                    current_ss.epoch)
            except Exception as error:
                self.fail("##(6)Error on current_ss.destroy: {}"
                          .format(str(error)))

        # (7)Check if still able to Open the destroyed snapshot and
        #    Verify the snapshot removed from the snapshot list
        try:
            obj.open()
            snap_handle7 = snapshot.open(coh, snapshot.epoch)
            thedata7 = self.container.read_an_obj(datasize, dkey, akey,
                                                  obj, txn=snap_handle7.value)
            obj.close()
        except Exception as error:
            self.fail("##(7)Error when retrieving the snapshot data: {}"
                      .format(str(error)))
        self.log.info("=(7)=>thedata_after_snapshot.destroyed.value[:200]= %s",
                      thedata7.value[:200])
        self.log.info("  ==>snapshot.epoch=     %s", snapshot.epoch)

        # Still able to open the snapshot and read data after destroyed.
        try:
            ss_list = snapshot.list(coh, snapshot.epoch)
            self.log.info(
                "  -->snapshot.list(coh, snapshot.epoch)= %s", ss_list)
        except Exception as error:
            self.fail("##(7)Error when calling the snapshot list: {}"
                      .format(str(error)))
