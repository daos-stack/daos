#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from avocado.core.exceptions import TestFail


class AutoOCSelectionTest(TestWithServers):
    """Automatic object class selection test.

    Test Class Description:
    Create a container with different RF and OC combinations. We expect some
    combinations to fail due to the restriction set by DAOS. (This is more like
    auto OC "restriction" rather than auto OC "selection".)

    :avocado: recursive
    """
    def test_oc_selection(self):
        """JIRA ID: DAOS-7595

        Test Description:
        Create a container with RF 0 to 4 - OC S1, RP_2G1, RP_3G1, RP_4G1, and
        RP_5G1.

        Use Cases:
        Create a pool. Then create each container and verify against its
        failure expected state.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=oc_selection,test_oc_selection
        """
        self.add_pool()

        # (Redundancy Factor, Object Class, Failure Expected)
        prop_oclasses = [
            ("rf:0", "S1", False),
            ("rf:0", "RP_2G1", False),
            ("rf:0", "RP_3G1", False),
            ("rf:0", "RP_4G1", False),
            ("rf:0", "RP_5G1", False),
            ("rf:1", "S1", True),
            ("rf:1", "RP_2G1", False),
            ("rf:1", "RP_3G1", False),
            ("rf:1", "RP_4G1", False),
            ("rf:1", "RP_5G1", False),
            ("rf:2", "S1", True),
            ("rf:2", "RP_2G1", True),
            ("rf:2", "RP_3G1", False),
            ("rf:2", "RP_4G1", False),
            ("rf:2", "RP_5G1", False),
            ("rf:3", "S1", True),
            ("rf:3", "RP_2G1", True),
            ("rf:3", "RP_3G1", True),
            ("rf:3", "RP_4G1", False),
            ("rf:3", "RP_5G1", False),
            ("rf:4", "S1", True),
            ("rf:4", "RP_2G1", True),
            ("rf:4", "RP_3G1", True),
            ("rf:4", "RP_4G1", True),
            ("rf:4", "RP_5G1", False)
        ]

        self.container = []
        errors = []

        # Create a container for each combination.
        for prop_oclass in prop_oclasses:
            properties = prop_oclass[0]
            oclass = prop_oclass[1]
            failure_expected = prop_oclass[2]

            try:
                self.log.info("Creating container with RF = %s, OC = %s", properties, oclass)
                self.container.append(
                    self.get_container(pool=self.pool, properties=properties, oclass=oclass))
                if failure_expected:
                    msg = ("Container create succeeded with invalid RF-OC "
                           "pair! RF = {}, OC = {}".format(properties, oclass))
                    errors.append(msg)
            except TestFail as error:
                self.log.info("error = %s", error)
                if not failure_expected:
                    msg = ("Container create failed with valid RF-OC pair! "
                           "RF = {}, OC = {}".format(properties, oclass))
                    errors.append(msg)

        if errors:
            self.fail("\n---- Errors detected! ----\n{}".format(
                "\n".join(errors)))
