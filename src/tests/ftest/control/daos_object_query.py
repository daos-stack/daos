#!/usr/bin/python
"""
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
"""
from apricot import TestWithServers
from daos_utils import DaosCommand
from general_utils import convert_list


class DaosObjectQuery(TestWithServers):
    """Test Class Description:
    This test partially covers SRS-12-0624.
    daos utility supports all command options except rollback, verify,
    list-keys and dump.

    Write an object with different object classes, query it with daos object
    query, and verify the output.

    Test with the following object classes.
    202: OC_S4, 214: OC_SX, 220: OC_RP_2G1, 221: OC_PR_2G2
    These classes are selected because they're used in other test and generate
    consistent query output. It would be better if we can understand more about
    object classes/layout, the command, and their relationship so that we can
    test with more classes and do better validation. However, due to the time
    constraint, we'll stick with these four classes and the test steps for now.

    From the command output, verify oid, grp_nr, and the list of grp-replica(s)
    groups.

    :avocado: recursive
    """

    def test_object_query(self):
        """
        JIRA ID: DAOS-4694
        Test Description: Test daos object query.
        :avocado: tags=all,container,hw,small,full_regression,daos_object_query
        """
        daos_cmd = DaosCommand(self.bin)
        errors = []

        # Create a pool and a container.
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
        # 202: OC_S4, 214: OC_SX, 220: OC_RP_2G1, 221: OC_PR_2G2
        # Other classes and more details are in src/include/daos_obj_class.h
        obj_classes = [202, 214, 220, 221]
        expected_replica_ones = {
            200: (0, 1), 202: (0, 2), 214: (0, 8), 220: (1, 1), 221: (2, 2)}

        for obj_class in obj_classes:
            # Write object.
            self.container.write_objects(obj_class=obj_class)

            # Verify oid values.
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
                "svc": convert_list(self.pool.svc_ranks),
                "cont": self.container.uuid,
                "oid": oid_concat
            }
            # Call dmg object query.
            query_output = daos_cmd.object_query(**kwargs)
            actual_oid_hi = query_output["oid"][0]
            actual_oid_lo = query_output["oid"][1]
            if str(expected_oid_hi) != actual_oid_hi:
                errors.append(
                    "Unexpected oid.hi! OC = {}; Expected = {}; "\
                    "Actual = {}".format(
                        obj_class, expected_oid_hi, actual_oid_hi))
            if str(expected_oid_lo) != actual_oid_lo:
                errors.append(
                    "Unexpected oid.lo! OC = {}; Expected = {}; "\
                    "Actual = {}".format(
                        obj_class, expected_oid_lo, actual_oid_lo))

            # Verify replica nums and grp_nr.
            # For grp_nr, check to see if grp 0 -> grp_nr - 1 exists. e.g., if
            # grp_nr is 4, we expect to see grp: 0, grp: 1, grp: 2, grp: 3.
            expected_grps = set(
                [nr for nr in range(int(query_output["grp_nr"]))])

            # For replica nums, check the total number of 1s on the left and
            # right because the sequence seems to be arbitrary.
            left = 0
            right = 0
            obj_groups = query_output["layout"]
            for obj_group in obj_groups:

                # Test grp_nr.
                if int(obj_group["grp"]) not in expected_grps:
                    errors.append(
                        "Unexpected grp sequence! OC = {}; {} is not in "\
                        "{}".format(obj_class, obj_group["grp"], expected_grps))
                else:
                    expected_grps.remove(int(obj_group["grp"]))

                # Accumulate replica 1s.
                replica_tuples = obj_group["replica"]
                for replica_tuple in replica_tuples:
                    left += int(replica_tuple[0])
                    right += int(replica_tuple[1])

            # Test replica's left and right 1 counts are expected.
            actual_replica_ones = (left, right)
            if expected_replica_ones[obj_class] != actual_replica_ones:
                errors.append(
                    "Unexpected replica values! OC = {}; Expected = {}; "\
                    "Actual = {}".format(
                        obj_class, expected_replica_ones[obj_class],
                        actual_replica_ones))

            # Check that we have seen all values in 0 -> grp_nr - 1.
            if expected_grps:
                errors.append(
                    "There is grp_nr we haven't seen! OC = {}; {}".format(
                        obj_class, expected_grps))

        if errors:
            self.log.info("--- Error List ---")
            for error in errors:
                self.log.info(error)
            self.fail("Error found!")
