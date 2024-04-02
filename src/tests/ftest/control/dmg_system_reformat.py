"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from exception_utils import CommandFailure
from general_utils import journalctl_time
from test_utils_pool import add_pool, get_size_params


class DmgSystemReformatTest(TestWithServers):
    """Test Class Description:

    Test to verify dmg storage format reformat option works as expected on a
    DAOS system after a controlled shutdown.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Create test-case-specific DAOS log files
        self.update_log_file_names()
        super().setUp()

    def test_dmg_system_reformat(self):
        """
        JIRA ID: DAOS-5415

        Test Description: Test dmg system reformat functionality.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,dmg,system_reformat
        :avocado: tags=DmgSystemReformatTest,test_dmg_system_reformat
        """
        dmg = self.get_dmg_command().copy()

        # Create pool using 90% of the available NVMe capacity
        pools = [add_pool(self, dmg=dmg)]

        self.log.info("Check that new pool will fail with DER_NOSPACE")
        dmg.exit_status_exception = False
        pools.append(add_pool(self, create=False, **get_size_params(pools[0])))
        try:
            pools[-1].create()
        except TestFail as error:
            self.log.info("Pool create failed: %s", str(error))
            if "-1007" not in str(error):
                self.fail("Pool create did not fail due to DER_NOSPACE!")
        dmg.exit_status_exception = True

        self.log.info("Stop running engine instances: 'dmg system stop'")
        dmg.system_stop(force=True)
        if dmg.result.exit_status != 0:
            self.fail("Detected issues performing a system stop: {}".format(
                dmg.result.stderr_text))

        # Remove pools and disable removing pools that about to be removed by formatting
        for pool in pools:
            pool.skip_cleanup()
        pools = []

        # Perform a dmg system erase to allow the dmg storage format to succeed
        self.log.info("Perform dmg system erase on all system ranks:")
        dmg.system_erase()
        if dmg.result.exit_status != 0:
            self.fail("Issues performing system erase: {}".format(
                dmg.result.stderr_text))

        self.log.info("Perform dmg storage format on all system ranks:")

        # Calling storage format after system stop too soon would fail, so
        # wait 10 sec and retry up to 4 times.
        count = 0
        while count < 4:
            try:
                dmg.storage_format(force=True)
                if dmg.result.exit_status != 0:
                    self.fail(
                        "Issues performing storage format --force: {}".format(
                            dmg.result.stderr_text))
                break
            except CommandFailure as error:
                self.log.info("Storage format failed. Wait 10 sec and retry. %s", error)
                count += 1
                time.sleep(10)

        # Check that engine starts up again
        self.log.info("<SERVER> Waiting for the engines to start")
        self.server_managers[-1].manager.timestamps["start"] = journalctl_time()
        self.server_managers[-1].detect_engine_start()

        # Check that we have cleared storage by checking pool list
        if dmg.get_pool_list_uuids():
            self.fail("Detected pools in storage after reformat: {}".format(
                dmg.result.stdout_text))

        # Create last pool now that memory has been wiped.
        pools.append(add_pool(self, connect=False, dmg=dmg))

        # Lastly, verify that last created pool is in the list
        pool_uuids = dmg.get_pool_list_uuids()
        self.assertEqual(
            pool_uuids[0].lower(), pools[-1].uuid.lower(), "{} missing from list".format(pools[-1]))
