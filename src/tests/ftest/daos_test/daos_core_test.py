#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

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

from daos_core_base import DaosCoreBase


class DaosCoreTest(DaosCoreBase):
    # pylint: disable=too-many-ancestors
    """Runs just the non-rebuild daos_test tests.

    :avocado: recursive
    """

    def test_daos_degraded_mode(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -d

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_degraded_mode
        """
        self.run_subtest()

    def test_daos_management(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -m

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_management
        """
        self.run_subtest()

    def test_daos_pool(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -p

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_pool
        """
        self.run_subtest()

    def test_daos_container(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -c

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_container
        """
        self.run_subtest()

    def test_daos_epoch(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -e

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_epoch
        """
        self.run_subtest()

    def test_daos_single_rdg_tx(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -t

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_single_rdg_tx
        """
        self.run_subtest()

    def test_daos_distributed_tx(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -T

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_distributed_tx
        """
        self.run_subtest()

    def test_daos_verify_consistency(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -V

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_verify_consistency
        """
        self.run_subtest()

    def test_daos_io(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -i

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_io
        """
        self.run_subtest()

    def test_daos_object_array(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -A

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_object_array
        """
        self.run_subtest()

    def test_daos_array(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -D

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_array
        """
        self.run_subtest()

    def test_daos_kv(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -K

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_kv
        """
        self.run_subtest()

    def test_daos_file_system(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -F

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_file_system
        """
        self.run_subtest()

    def test_daos_capability(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -C

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_capability
        """
        self.run_subtest()

    def test_daos_epoch_recovery(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -o

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_epoch_recovery
        """
        self.run_subtest()

    def test_daos_md_replication(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -R

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_md_replication
        """
        self.run_subtest()

    def test_daos_rebuild_simple(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -v

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_rebuild_simple
        """
        self.run_subtest()

    def test_daos_drain_simple(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -b

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_drain_simple
        """
        self.run_subtest()

    def test_daos_oid_allocator(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -O

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_oid_allocator
        """
        self.run_subtest()

    def test_daos_checksum(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -z

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_checksum
        """
        self.run_subtest()

    def test_daos_rebuild_ec(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -S

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_rebuild_ec
        """
        self.run_subtest()

    def test_daos_aggregate_ec(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -Z

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_aggregate_ec
        """
        self.run_subtest()

    def test_daos_degraded_ec_0to6(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -X -u subtests="0-6"

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_degraded_ec_1
        """
        self.run_subtest()

    def test_daos_degraded_ec_8to22(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -X -u subtests="8-22"

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_degraded_ec_2
        """
        self.run_subtest()

    def test_daos_dedup(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -U

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_dedup
        """
        self.run_subtest()
