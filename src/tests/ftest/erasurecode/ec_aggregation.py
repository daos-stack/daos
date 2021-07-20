#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ec_utils import ErasureCodeIor, check_aggregation_status

class EcodAggregationOff(ErasureCodeIor):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object with different
                            Aggregation mode.
    :avocado: recursive
    """

    def test_ec_aggregation_disabled(self):
        """Jira ID: DAOS-7325.

        Test Description: Test Erasure code object aggregation disabled mode
                          with IOR.
        Use Case: Create the pool, disabled aggregation, run IOR with supported
                  EC object type. Verify that Aggregation should not
                  triggered. Verify the IOR read data at the end.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,aggregation,ec_array,ec_aggregation
        :avocado: tags=ec_aggregation_disabled
        """
        # Disable the aggregation
        self.pool.set_property("reclaim", "disabled")
        self.pool.connect()
        print("pool_percentage Before = {} ".format(
            self.pool.pool_percentage_used()))

        # Write the IOR data set with given all the EC object type
        self.ior_write_dataset()

        # Verify if Aggregation is getting started
        if any(check_aggregation_status(self.pool).values()) is True:
            self.fail("Aggregation should not happens...")

        # Read IOR data and verify content
        self.ior_read_dataset()

    def test_ec_aggregation_default(self):
        """Jira ID: DAOS-7325.

        Test Description: Test Erasure code object aggregation enabled(default)
                          mode with IOR.
        Use Case: Create the pool, run IOR with supported
                  EC object type classes. Verify the Aggregation gets
                  triggered and space is getting reclaimed.
                  Verify the IOR read data at the end.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,aggregation,ec_array,ec_aggregation
        :avocado: tags=ec_aggregation_default
        """
        self.pool.connect()
        print("pool_percentage Before = {} ".format(
            self.pool.pool_percentage_used()))

        for oclass in self.obj_class:
            for sizes in self.ior_chu_trs_blk_size:
                # Write single IOR
                self.ior_write_single_dataset(oclass, sizes)

                # Verify if Aggregation is getting started
                if not any(check_aggregation_status(self.pool).values()):
                    self.fail("Aggregation failed to start..")

                # Read single IOR
                self.ior_read_single_dataset(oclass, sizes)

    def test_ec_aggregation_time(self):
        """Jira ID: DAOS-7325.

        Test Description: Test Erasure code object aggregation time mode
                          with IOR.
        Use Case: Create the pool,Set aggregation as time mode.
                  run IOR with supported EC object type classes.
                  Verify the Aggregation gets triggered in parallel and space
                  is getting reclaimed. Verify the IOR read data at the end.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,aggregation,ec_array,ec_aggregation
        :avocado: tags=ec_aggregation_time
        """
        # Set time mode aggregation
        self.pool.set_property("reclaim", "time")
        self.pool.connect()
        print("pool_percentage Before = {} ".format(
            self.pool.pool_percentage_used()))

        # Write the IOR data set with given all the EC object type
        self.ior_write_dataset()

        # Verify if Aggregation is getting started
        if not any(check_aggregation_status(self.pool).values()):
            self.fail("Aggregation failed to start..")

        # Read IOR data and verify content
        self.ior_read_dataset()
