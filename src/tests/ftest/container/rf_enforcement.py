#!/usr/bin/python
"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from container_rf_test_base import ContRedundancyFactor


class ContRfEnforce(ContRedundancyFactor):
    # pylint: disable=too-many-ancestors
    """Test Container redundancy factor enforcement with oclass traffic
       and rebuild.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Rebuild Container RF with ObjClass Write object."""
        super().__init__(*args, **kwargs)
        self.daos_cmd = None

    def test_container_redundancy_factor_oclass_enforcement(self):
        """Jira ID:
        DAOS-6267: Verify that a container can be created with and enforces
                   a redundancy factor of 0.
        DAOS-6268: Verify container with RF 0 created, deleted and error
                   reported as target fails.
        DAOS-6269: Verify container with RF 2 enforces object creation limits.
        Description:
            Test step:
                (1)Create pool and container with redundant factor
                   Start background IO object write, and verify the
                   container redundant factor vs. different object_class
                   traffic.
                   Continue following steps for the positive testcases.
                (2)Check for container initial rf and health-status.
        Use Cases:
            Verify container RF enforcement with different object class
            traffic, positive test of rebuild with server failure and read
            write io verification.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=container_rf,cont_rf_oclass_enforcement
        :avocado: tags=test_container_redundancy_factor_oclass_enforcement
        """
        self.mode = "cont_rf_enforcement"
        self.execute_cont_rf_test()
