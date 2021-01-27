#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from rebuild_test_base import RebuildTestBase


class RebuildDeleteObjects(RebuildTestBase):
    """Test class for deleting objects during pool rebuild.

    Test Class Description:
        This class contains tests for deleting objects from a container during
        rebuild.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a RebuildDeleteObjects object."""
        super(RebuildDeleteObjects, self).__init__(*args, **kwargs)
        self.punched_indices = None
        self.punched_qty = 0
        self.punch_type = None

    def execute_during_rebuild(self):
        """Delete half of the objects from the container during rebuild."""
        if self.punch_type == "object":
            # Punch half of the objects
            self.punched_indices = [
                index for index in range(self.container.object_qty.value)
                if index % 2]
            self.punched_qty = self.container.punch_objects(
                self.punched_indices)

        elif self.punch_type == "record":
            # Punch half of the records in each object
            self.punched_indices = [
                index for index in range(self.container.record_qty.value)
                if index % 2]
            self.punched_qty = self.container.punch_records(
                self.punched_indices)
            # self.punched_qty /= self.container.object_qty.value

    def verify_container_data(self, txn=0):
        """Verify the container data.

        Args:
            txn (int, optional): transaction timestamp to read. Defaults to 0.
        """
        # Verify the expected number of objects/records were punched
        if self.punch_type == "object":
            expected_qty = len(self.punched_indices)
        elif self.punch_type == "record":
            expected_qty = \
                 len(self.punched_indices) * self.container.object_qty.value
        else:
            expected_qty = 0
        self.assertEqual(
            expected_qty, self.punched_qty,
            "Error punching {}s during rebuild: {}/{}".format(
                self.punch_type, self.punched_qty, expected_qty))

        # Read objects from the last transaction
        super(RebuildDeleteObjects, self).verify_container_data(txn)

    def test_rebuild_delete_objects(self):
        """JIRA ID: DAOS-2572.

        Test Description:
            Delete objects during rebuild. Rebuild should complete successfully
            and only the remaining data should be accessible and it should only
            exist on the rebuild target and non-excluded, original targets. The
            data in the deleted objects should not be accessible.

        Use Cases:
            foo

        :avocado: tags=all,medium,full_regression,rebuild,rebuilddeleteobject
        """
        self.punch_type = "object"
        self.execute_rebuild_test()

    def test_rebuild_delete_records(self):
        """JIRA ID: DAOS-2574.

        Test Description:
            Delete records during rebuild. Rebuild should complete successfully
            and only the remaining data should be accessible and it should only
            exist on the rebuild target and non-excluded, original targets. The
            data in the deleted records should not be accessible.

        Use Cases:
            foo

        :avocado: tags=all,medium,full_regression,rebuild,rebuilddeleterecord
        """
        self.punch_type = "record"
        self.execute_rebuild_test()
