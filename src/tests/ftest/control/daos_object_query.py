#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from daos_utils import DaosCommand


class DaosObjectQuery(TestWithServers):
    """Test Class Description:
    This test partially covers SRS-12-0624.
    daos utility supports all command options except rollback, verify,
    list-keys and dump.

    Write an object with different object classes, query it with daos object
    query, and verify the output.

    Test with the following object classes.
    200: OC_S1, 240: OC_RP_2G1, 241: OC_RP_2G2, 280: OC_PR_3G1

    From the command output, verify oid, grp_nr, and the list of grp-replica(s)
    groups.

    Sample daos object query output.
    oid: 1152922453794619396.1 ver 0 grp_nr: 2
    grp: 0
    replica 0 1
    replica 1 0
    grp: 1
    replica 0 0
    replica 1 1

    :avocado: recursive
    """

    def test_object_query(self):
        """JIRA ID: DAOS-4694
        Test Description: Test daos object query.
        :avocado: tags=all,full_regression
        :avocado: tags=control
        :avocado: tags=daos_object_query
        """
        daos_cmd = DaosCommand(self.bin)
        errors = []

        # Create a pool and a container. Specify --oclass, which will be used
        # when writing object.
        self.add_pool()
        self.add_container(self.pool)
        self.pool.connect()
        self.container.open(pool_handle=self.pool.pool.handle.value)

        # Prepare to write object.
        self.container.data_size.update(100)
        # Write 1 object.
        self.container.object_qty.update(1)
        # Record quantity is the number of times we write the object.
        self.container.record_qty.update(1)
        # These determine the size of the keys. Use the frequently used values.
        self.container.akey_size.update(4)
        self.container.dkey_size.update(4)

        # Object class defines the number of replicas, groups, etc.
        # Other classes and more details are in src/include/daos_obj_class.h
        # This mapping could change, so check daos_obj_class.h when this test
        # fails.
        class_name_to_code = {
            "S1": 16777217,
            "RP_2G1": 134217729,
            "RP_2G2": 134217730,
            "RP_3G1": 150994945
        }
        class_name_to_group_num = {
            "S1": 1,
            "RP_2G1": 1,
            "RP_2G2": 2,
            "RP_3G1": 1
        }
        class_name_to_replica_rows = {
            "S1": 1,
            "RP_2G1": 2,
            "RP_2G2": 4,
            "RP_3G1": 3
        }

        # Write object. Use the same object class as the one used during the
        # container create. We need the corresponding integer value for
        # write_objects.
        class_name = self.container.oclass.value
        obj_class = class_name_to_code[class_name]
        self.container.write_objects(obj_class=obj_class)

        # Verify oid values. First, get the expected oid hi and lo.
        # written_data is a list of TestContainerData, which has DaosObj as
        # member. This is what we need to obtain oid values.
        obj = self.container.written_data[-1].obj
        obj.close()
        expected_oid_hi = obj.c_oid.hi
        expected_oid_lo = obj.c_oid.lo
        self.log.info("oid.hi = %s", expected_oid_hi)
        self.log.info("oid.lo = %s", expected_oid_lo)
        oid_concat = "{}.{}".format(expected_oid_hi, expected_oid_lo)
        kwargs = {
            "pool": self.pool.uuid,
            "cont": self.container.uuid,
            "oid": oid_concat
        }

        # Call daos object query and verify oid values.
        query_output = daos_cmd.object_query(**kwargs)
        actual_oid_hi = query_output["oid"][0]
        actual_oid_lo = query_output["oid"][1]
        if str(expected_oid_hi) != actual_oid_hi:
            errors.append(
                "Unexpected oid.hi! OC = {}; Expected = {}; "
                "Actual = {}".format(
                    obj_class, expected_oid_hi, actual_oid_hi))
        if str(expected_oid_lo) != actual_oid_lo:
            errors.append(
                "Unexpected oid.lo! OC = {}; Expected = {}; "
                "Actual = {}".format(
                    obj_class, expected_oid_lo, actual_oid_lo))

        # Verify grp_nr. Expected group number is the number that comes after G.
        expected_group_num = class_name_to_group_num[class_name]
        actual_group_num = query_output["grp_nr"]
        if str(expected_group_num) != actual_group_num:
            errors.append(
                "Unexpected grp_nr! Class = {}; Expected = {}; "
                "Actual = {}".format(
                    class_name, expected_group_num, actual_group_num))

        # Verify number of replica rows, which should be the multiplication of
        # replication (num before G) and group number (num after G). For S1,
        # there's 1 group and no replication, so the row count should be 1.
        expected_replica_rows = class_name_to_replica_rows[class_name]
        obj_layout = query_output["layout"]
        actual_replica_rows = 0
        actual_group_rows = 0
        for layout in obj_layout:
            actual_replica_rows += len(layout["replica"])
            actual_group_rows += 1
        if expected_replica_rows != actual_replica_rows:
            errors.append(
                "Unexpected replica row count! Class = {}; Expected = {}; "
                "Actual = {}".format(
                    class_name, expected_replica_rows, actual_replica_rows))
        # Also verify the number of group rows; rows that start with "grp".
        if expected_group_num != actual_group_rows:
            errors.append(
                "Unexpected group row count! Class = {}; Expected = {}; "
                "Actual = {}".format(
                    class_name, expected_group_num, actual_group_rows))

        # Print accumulated errors.
        if errors:
            self.log.info("--- Error List ---")
            for error in errors:
                self.log.info(error)
            self.fail("Error found!")
