"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from container_rf_test_base import ContRedundancyFactor


class RbldContRfTest(ContRedundancyFactor):
    """Test container redundancy factor with various rebuilds.

    :avocado: recursive
    """

    def test_rebuild_with_container_rf(self):
        """Jira ID:
        DAOS-6270: container with RF 2 can lose up to 2 concurrent
                   servers without err.
        DAOS-6271: container with RF 2 error reported after more than
                   2 concurrent servers failure occurred.
        Description:
            Test step:
                (1)Create pool and container with redundant factor
                   Start background IO object write.
                (2)Check for container initial rf and health-status.
                (3)Ranks rebuild start simultaneously.
                (4)Check for container rf and health-status after the
                   rebuild started.
                (5)Check for container rf and health-status after the
                   rebuild completed.
                (6)Check for pool and container info after rebuild.
                (7)Verify container io object write if the container is
                   healthy.
        Use Cases:
            Verify container RF with rebuild with multiple server failures.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container,rebuild,container_rf
        :avocado: tags=RbldContRfTest,test_rebuild_with_container_rf
        """
        self.execute_cont_rf_test(mode="cont_rf_with_rebuild")
