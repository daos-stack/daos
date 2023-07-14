"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_core_base import DaosCoreBase


class DaosCoreTest(DaosCoreBase):
    # pylint: disable=too-many-ancestors,too-many-public-methods
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
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_degraded_mode
        """
        self.run_subtest()

    def test_daos_management(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -m

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_management
        """
        self.run_subtest()

    def test_daos_pool(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -p

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_pool
        """
        self.run_subtest()

    def test_daos_container(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -c

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_container
        """
        self.run_subtest()

    def test_daos_epoch(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -e

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_epoch
        """
        self.run_subtest()

    def test_daos_single_rdg_tx(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -t

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_single_rdg_tx
        """
        self.run_subtest()

    def test_daos_distributed_tx(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -T

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_distributed_tx
        """
        self.run_subtest()

    def test_daos_verify_consistency(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -V

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_verify_consistency
        """
        self.run_subtest()

    def test_daos_io(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -i

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_io
        """
        self.run_subtest()

    def test_daos_ec_io(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -i -l"EC_4P2G1"

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_io,test_daos_ec_io
        """
        self.run_subtest()

    def test_daos_ec_obj(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -I

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_io,test_daos_ec_obj
        """
        self.run_subtest()

    def test_daos_object_array(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -A

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_object_array
        """
        self.run_subtest()

    def test_daos_array(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -D

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_array
        """
        self.run_subtest()

    def test_daos_kv(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -K

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_kv
        """
        self.run_subtest()

    def test_daos_capability(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -C

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_capability
        """
        self.run_subtest()

    def test_daos_epoch_recovery(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -o

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_epoch_recovery
        """
        self.run_subtest()

    def test_daos_md_replication(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -R

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_md_replication
        """
        self.run_subtest()

    def test_daos_rebuild_simple(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -v

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_rebuild_simple
        """
        self.run_subtest()

    def test_daos_drain_simple(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -b

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_drain_simple
        """
        self.run_subtest()

    def test_daos_extend_simple(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -B

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_extend_simple
        """
        self.run_subtest()

    def test_daos_oid_allocator(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -O

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_oid_allocator
        """
        self.run_subtest()

    def test_daos_checksum(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -z

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_checksum
        """
        self.run_subtest()

    def test_daos_rebuild_ec(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -S

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_rebuild_ec
        """
        self.run_subtest()

    def test_daos_aggregate_ec(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -Z

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_aggregate_ec
        """
        self.run_subtest()

    def test_daos_degraded_ec(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -X

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_degraded_ec
        """
        self.run_subtest()

    def test_daos_dedup(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -U

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_dedup
        """
        self.run_subtest()

    def test_daos_upgrade(self):
        """Jira ID: DAOS-10681

        Test Description:
            Run daos_test -G

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test,test_daos_upgrade
        """
        self.run_subtest()

    def test_daos_pipeline(self):
        """Jira ID: DAOS-13160

        Test Description:
            Run daos_test -P

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=daos_test,daos_core_test
        :avocado: tags=DaosCoreTest,test_daos_pipeline
        """
        self.run_subtest()
