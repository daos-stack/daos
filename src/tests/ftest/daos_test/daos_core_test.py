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

    Test ID: DAOS-1568
    Test Description: Run daos_test tests/subtests.

    Use Cases: core tests for daos_test

    :avocado: recursive
    """

    def test_degraded_mode(self):
        """Run daos_test -d

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "d", "DAOS Degraded-mode", 450)

    def test_management(self):
        """Run daos_test -m

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "m", "DAOS Management", 110)

    def test_pool(self):
        """Run daos_test -p

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "p", "DAOS Pool", 120)

    def test_container(self):
        """Run daos_test -c

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "c", "DAOS Container", 135)

    def test_epoch(self):
        """Run daos_test -e

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "e", "DAOS Epoch", 125)

    def test_single_rdg_tx(self):
        """Run daos_test -t

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "t", "DAOS Single RDG TX", 60)

    def test_distributed_tx(self):
        """Run daos_test -T

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "T", "DAOS Distributed TX", 60)

    def test_verify_consistency(self):
        """Run daos_test -V

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "V", "DAOS Verify Consistency", 105)

    def test_io(self):
        """Run daos_test -i

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "i", "DAOS IO", 280)

    def test_object_array(self):
        """Run daos_test -A

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "A", "DAOS Object Array", 105)

    def test_array(self):
        """Run daos_test -D

        Test ID: DAOS-1568
        Test Description: Run daos_test tests/subtests.

        Use Cases: core tests for daos_test

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "D", "DAOS Array", 106)

    def test_kv(self):
        """Run daos_test -K

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "K", "DAOS KV", 105)

    def test_file_system(self):
        """Run daos_test -F

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "F", "DAOS File System", 140)

    def test_capability(self):
        """Run daos_test -C

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "C", "DAOS Capability", 104)

    def test_epoch_recovery(self):
        """Run daos_test -o

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "o", "DAOS Epoch Recovery", 104)

    def test_md_replication(self):
        """Run daos_test -R

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "R", "DAOS MD Replication", 104)

    def test_rebuild_simple(self):
        """Run daos_test -v

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "v", "DAOS Rebuild Simple", 500)

    def test_drain_simple(self):
        """Run daos_test -b

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "b", "DAOS Drain Simple", 500)

    def test_oid_allocator(self):
        """Run daos_test -O

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "O", "DAOS OID Allocator", 320)

    def test_checksum(self):
        """Run daos_test -z

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "z", "DAOS Checksum", 240)

    def test_rebuild_ec(self):
        """Run daos_test -S

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "S", "DAOS Rebuild EC", 900)

    def test_aggregate_ec(self):
        """Run daos_test -Z

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "Z", "DAOS Aggregate EC", 60)

    def test_degraded_ec_0to6(self):
        """Run daos_test -X

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "X", "DAOS Degraded EC 0-6", 900,
                                 "-u subtests='0-6'")

    def test_degraded_ec_8to22(self):
        """Run daos_test -X

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "X", "DAOS Degraded EC 8-22", 900,
                                 "-u subtests='8-22'")

    def test_dedup(self):
        """Run daos_test -U

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test
        """
        DaosCoreBase.run_subtest(self, "U", "DAOS Dedup", 220)
