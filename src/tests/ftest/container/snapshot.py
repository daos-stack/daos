"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from general_utils import get_random_bytes
from pydaos.raw import DaosApiError, DaosSnapshot, c_uuid_to_str
from test_utils_container import add_container
from test_utils_pool import add_pool


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

    def create_container(self):
        """Create a pool and container.

        Returns:
            TestContainer: a new container object
        """
        self.log_step('Creating pool and container')
        self.log.info("self.context= %s", self.context)
        pool = add_pool(self)
        container = add_container(self, pool)

        self.log_step('Querying container and verifying UUID matches')
        container.open()
        container.container.query()
        if container.container.get_uuid_str() != c_uuid_to_str(container.container.info.ci_uuid):
            self.fail("Container UUID does not match the one in info.")
        return container

    def display_snapshot(self, snapshot):
        """
        To display the snapshot information.
        Args:
            snapshot: snapshot handle to be displayed.
        Return:
            none.
        """
        self.log.info("--display_snapshot-------------------------------")
        self.log.info("snapshot:                            %s", snapshot)
        self.log.info("snapshot.context:                    %s", snapshot.context)
        self.log.info("snapshot.context.libdaos:            %s", snapshot.context.libdaos)
        self.log.info("snapshot.context.libtest:            %s", snapshot.context.libtest)
        self.log.info("snapshot.context.ftable:             %s", snapshot.context.ftable)
        self.log.info(
            "snapshot.context.ftable[list-attr]:  %s", snapshot.context.ftable["list-attr"])
        self.log.info(
            "snapshot.context.ftable[test-event]: %s", snapshot.context.ftable["test-event"])
        self.log.info("snapshot.name:                       %s", snapshot.name)
        self.log.info("snapshot.epoch:                      %s", snapshot.epoch)
        self.log.info("-------------------------------------------------")

    def take_snapshot(self, handle):
        """To take a snapshot on the container on current epoch.

        Args:
            handle (object): the container handle from which to take a snapshot

        Returns:
            DaosSnapshot: the snapshot taken
        """
        snapshot = DaosSnapshot(self.context)
        snapshot.create(handle)
        self.display_snapshot(snapshot)
        return snapshot

    def test_snapshot_negative_cases(self):
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
        :avocado: tags=Snapshot,test_snapshot_negative_cases
        """

        # DAOS-1322 Create a new container, verify snapshot state as expected
        #           for a brand new container.
        container = self.create_container()

        self.log_step('Take a snapshot of the newly created container.')
        snapshot = self.take_snapshot(container.container.coh)

        # (1)Create an object, write some data into it, and take a snapshot
        self.log_step('Write data to a new object')
        obj_cls = self.params.get("obj_class", '/run/object_class/*')
        akey = self.params.get("akey", '/run/snapshot/*', default="akey")
        dkey = self.params.get("dkey", '/run/snapshot/*', default="dkey")
        akey = akey.encode("utf-8")
        dkey = dkey.encode("utf-8")
        data_size = self.params.get("test_datasize", '/run/snapshot/*', default=150)
        data = b"--->>>Happy Daos Snapshot-Create Negative Testing " + \
               b"<<<---" + get_random_bytes(self.random.randint(1, data_size))
        try:
            obj = container.container.write_an_obj(data, len(data) + 1, dkey, akey, obj_cls=obj_cls)
            obj.close()
        except DaosApiError as error:
            self.log.error(str(error))
            self.fail('Test failed during the initial object write')

        # Take a snapshot of the container
        self.log_step('Take a snapshot of the container with data')
        snapshot = self.take_snapshot(container.container.coh)
        self.log.info('  snapshot.epoch: %s', snapshot.epoch)

        # Verify the snapshot is working properly.
        self.log_step('Verify the snapshot of the container data')
        try:
            obj.open()
            snap_handle = snapshot.open(container.container.coh, snapshot.epoch)
            data2 = container.container.read_an_obj(
                len(data) + 1, dkey, akey, obj, txn=snap_handle.value)
        except Exception as error:
            self.log.error(str(error))
            self.fail('Error verifying the snapshot of the container data')
        if data2.value != data:
            self.log.info('The snapshot of the container data mismatched:')
            self.log.info('  snapshot_list[ind]:    %s', snapshot)
            self.log.info('  snapshot.epoch:        %s', snapshot.epoch)
            self.log.info('  written thedata[:200]: %s', data[:200])
            self.log.info('  thedata2.value[:200]:  %s', data2.value[:200])
            self.fail('The data in the snapshot is not the same as the original data')
        self.log.info('Snapshot data matches the data originally written.')

        # Test snapshot with an invalid container handle
        self.log_step(f'Taking a snapshot with an invalid container handle ({container.container})')
        try:
            self.take_snapshot(container.container)
            self.fail('Unexpected failure detected when taking a snapshot with an invalid handle')
        except Exception as error:
            self.log.debug('Exception detected: %s', error)
            self.log.info('Expected failure detected when taking a snapshot with an invalid handle')

        # Test snapshot with a NULL container handle
        self.log_step('Taking a snapshot with an NULL container handle')
        try:
            self.take_snapshot(None)
            self.fail('Unexpected failure detected when taking a snapshot with an NULL handle')
        except Exception as error:
            self.log.debug('Exception detected: %s', error)
            self.log.info(
                'Exception detected when destroying a snapshot with an invalid handle: %s', error)
            self.log.info('Expected failure detected when taking a snapshot with an NULL handle')

        # DAOS-1392 destroy snapshot with an invalid handle
        self.log_step('Destroying a snapshot with an invalid container handle')
        try:
            snapshot.destroy(None, snapshot.epoch)
            self.fail(
                'Unexpected failure detected when destroying a snapshot with an invalid handle')
        except Exception as error:
            self.log.debug('Exception detected: %s', error)
            if 'RC: -1002' not in str(error):
                self.fail('RC -1002 not detected when destroying a snapshot with an invalid handle')
            else:
                self.log.info(
                    'Expected failure detected when destroying a snapshot with an invalid handle')

        # DAOS-1388 Verify snap_list bad parameter behavior
        self.log_step('Listing a snapshot with an invalid container handle')
        try:
            snapshot.list(None, 0)
            self.fail('Unexpected failure detected when listing a snapshot with an invalid handle')
        except Exception as error:
            self.log.debug('Exception detected: %s', error)
            if 'RC: -1002' not in str(error):
                self.fail('RC -1002 not detected when listing a snapshot with an invalid handle')
            else:
                self.log.info(
                    'Expected failure detected when listing a snapshot with an invalid handle')

        self.log.info('Test passed')

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
        self.log.debug("Snapshot number:           %s", ss_index)
        if len(test_data) < ss_index - 1:
            self.log.debug("  Unable to display test_data info, index out of range.")
        else:
            ind = ss_index - 1
            self.log.debug("  container_coh:           %s", test_data[ind]["coh"])
            self.log.debug("  snapshot:                %s", test_data[ind]["snapshot"])
            self.log.debug("  snapshot.epoch:          %s", test_data[ind]["snapshot"].epoch)
            self.log.debug("  data obj:                %s", test_data[ind]["tst_obj"])
            self.log.debug("  snapshot tst_data_size:  %s", len(test_data[ind]["tst_data"]) + 1)
            self.log.debug("  original tst_data[:200]: %s", test_data[ind]["tst_data"][:200])

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
        :avocado: tags=Snapshot,test_snapshots
        """
        container = self.create_container()
        test_data = []
        ss_number = 0
        obj_cls = self.params.get("obj_class", '/run/object_class/*')
        akey = self.params.get("akey", '/run/snapshot/*', default="akey")
        dkey = self.params.get("dkey", '/run/snapshot/*', default="dkey")
        akey = akey.encode("utf-8")
        dkey = dkey.encode("utf-8")
        data_size = self.params.get("test_datasize", '/run/snapshot/*', default=150)
        snapshot_loop = self.params.get("num_of_snapshot", '/run/snapshot/*', default=3)
        #
        # Test loop for create, modify, and snapshot object in the DAOS container.
        #
        while ss_number < snapshot_loop:
            # Create an object, write some data into it, and take a snapshot
            ss_number += 1
            self.log_step(f'Write data to a container object - loop {ss_number}/{snapshot_loop}')
            data = b"--->>>Happy Daos Snapshot Testing " + \
                   str(ss_number).encode("utf-8") + \
                   b"<<<---" + get_random_bytes(self.random.randint(1, data_size))
            try:
                obj = container.container.write_an_obj(
                    data, len(data) + 1, dkey, akey, obj_cls=obj_cls)
                obj.close()
            except DaosApiError as error:
                self.log.debug('Exception detected: %s', error)
                self.fail(f'Test failed during the initial object write in loop {ss_number}')

            # Take a snapshot of the container
            self.log_step(f'Take a snapshot of the container - loop {ss_number}/{snapshot_loop}')
            snapshot = self.take_snapshot(container.container.coh)

            # Save snapshot test data
            test_data.append(
                {"coh": container.container.coh,
                 "tst_obj": obj,
                 "snapshot": snapshot,
                 "tst_data": data})

            # Make changes to the data object. The write_an_obj function does a commit when the
            # update is complete
            self.log_step(
                f'Write additional data to a container object - loop {ss_number}/{snapshot_loop}')
            num_transactions = more_transactions = 200
            while more_transactions:
                size = self.random.randint(1, 250) + 1
                new_data = get_random_bytes(size)
                try:
                    new_obj = container.container.write_an_obj(
                        new_data, size, dkey, akey, obj_cls=obj_cls)
                    new_obj.close()
                except Exception as error:
                    self.log.debug('Exception detected: %s', error)
                    self.fail(
                        'Test failed writing additional data to a container object - loop '
                        f'{ss_number}/{snapshot_loop}')
                more_transactions -= 1

            # Verify the data in the snapshot is the original data.
            #    Get a handle for the snapshot and read the object at dkey, akey
            #    Compare it to the originally written data.
            self.log_step(
                f'Retrieve the snapshot data from the container - loop {ss_number}/{snapshot_loop}')
            try:
                obj.open()
                snap_handle = snapshot.open(container.container.coh, snapshot.epoch)
                data3 = container.container.read_an_obj(
                    len(data) + 1, dkey, akey, obj, txn=snap_handle.value)
                obj.close()
            except Exception as error:
                self.log.debug('Exception detected: %s', error)
                self.fail(
                    f'Test failed retrieving the snapshot data - loop {ss_number}/{snapshot_loop}')

            self.log_step(
                'Verify the data in the snapshot matches the original data - loop '
                f'{ss_number}/{snapshot_loop}')
            if data3.value != data:
                self.display_snapshot_test_data(test_data, ss_number)
                self.log.debug("  data3.value[:200]:       %s", data3.value[:200])
                self.fail("The data in the snapshot is not the same as the original data")
            self.log.info("The snapshot data matches the data originally written.")

            # List the snapshot and make sure it reflects the original epoch
            self.log_step(
                'List the snapshot and make sure it reflects the original epoch - loop '
                f'{ss_number}/{snapshot_loop}')
            try:
                ss_list = snapshot.list(container.container.coh, snapshot.epoch)
                self.log.info("  snapshot.list(): %s", ss_list)
                self.log.info("  snapshot.epoch:  %s", snapshot.epoch)

            except Exception as error:
                self.log.error(str(error))
                self.fail(f'Test failed listing the snapshot - loop {ss_number}/{snapshot_loop}')

            self.log.info(
                "After %s additional commits the snapshot is still available", num_transactions)

        # (5)Verify the snapshots data
        #    Step(5) and (6), test loop to perform multiple snapshots data
        #    verification and snapshot destroy.
        #    Use current_ss for the individual snapshot object.
        for ss_number in range(snapshot_loop - 1, 0, -1):
            ind = ss_number - 1
            self.log_step(f'Retrieving snapshot {ss_number} data')
            coh = test_data[ind]["coh"]
            current_ss = test_data[ind]["snapshot"]
            obj = test_data[ind]["tst_obj"]
            tst_data = test_data[ind]["tst_data"]
            try:
                obj.open()
                snap_handle5 = current_ss.open(coh, current_ss.epoch)
                data5 = container.container.read_an_obj(
                    len(tst_data) + 1, dkey, akey, obj, txn=snap_handle5.value)
                obj.close()
            except Exception as error:
                self.log.debug('Exception detected (snapshot %s): %s', ss_number, error)
                self.display_snapshot_test_data(test_data, ss_number)
                self.fail(f'Error retrieving snapshot {ss_number} data')

            self.log_step(f'Verifying snapshot {ss_number} data')
            if data5.value != tst_data:
                self.display_snapshot_test_data(test_data, ss_number)
                self.log.debug("  snapshot tst_data[:200]: %s", data5.value[:200])
                self.fail(f'Snapshot {ss_number} data does not match the original data')
            self.log.info('Snapshot %s data does match the original data', ss_number)

            # Destroy the individual snapshot
            self.log_step(f'Destroying snapshot {ss_number} epoch {current_ss.epoch}')
            try:
                current_ss.destroy(coh, current_ss.epoch)
                self.log.info("  snapshot.epoch %s successfully destroyed", current_ss.epoch)
            except Exception as error:
                self.log.debug('Exception detected: %s', error)
                self.fail(f'Error destroying snapshot {ss_number}')

        # (7)Check if still able to Open the destroyed snapshot and
        #    Verify the snapshot removed from the snapshot list
        self.log_step('Retrieving destroyed snapshot data')
        try:
            obj.open()
            snap_handle7 = snapshot.open(coh, snapshot.epoch)
            data7 = container.container.read_an_obj(
                len(tst_data) + 1, dkey, akey, obj, txn=snap_handle7.value)
            obj.close()
        except Exception as error:
            self.log.debug('Exception detected: %s', error)
            self.fail('Error retrieving destroyed snapshot data')

        self.log.info("  data_after_snapshot.destroyed.value[:200]: %s", data7.value[:200])
        self.log.info("  snapshot.epoch:                            %s", snapshot.epoch)

        # Still able to open the snapshot and read data after destroyed.
        self.log_step('Listing the destroyed snapshot data')
        try:
            ss_list = snapshot.list(coh, snapshot.epoch)
            self.log.info("  snapshot.list(coh, snapshot.epoch):    %s", ss_list)
        except Exception as error:
            self.log.debug('Exception detected: %s', error)
            self.fail('Error listing destroyed snapshot data')

        self.log.info('Test passed')
