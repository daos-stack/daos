#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from avocado.core.exceptions import TestFail
from command_utils_base import CommandFailure
from pool_test_base import PoolTestBase


class DmgSystemReformatTest(PoolTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:

    Test to verify dmg storage format reformat option works as expected on a
    DAOS system after a controlled shutdown.

    :avocado: recursive
    """
    def test_dmg_system_reformat(self):
        """
        JIRA ID: DAOS-5415

        Test Description: Test dmg system reformat functionality.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=control,dmg_system_reformat,dmg
        """
        # Create pool using 90% of the available NVMe capacity
        self.add_pool_qty(1)

        self.log.info("Check that new pool will fail with DER_NOSPACE")
        self.get_dmg_command().exit_status_exception = False
        self.add_pool_qty(1, create=False)
        try:
            self.pool[-1].create()
        except TestFail as error:
            self.log.info("Pool create failed: %s", str(error))
            if "-1007" not in self.get_dmg_command().result.stderr_text:
                self.fail("Pool create did not fail due to DER_NOSPACE!")
        self.get_dmg_command().exit_status_exception = True

        self.log.info("Stop running engine instances: 'dmg system stop'")
        self.get_dmg_command().system_stop(force=True)
        if self.get_dmg_command().result.exit_status != 0:
            self.fail("Detected issues performing a system stop: {}".format(
                self.get_dmg_command().result.stderr_text))

        # Remove pools
        self.pool = []

        # Perform a dmg system erase to allow the dmg storage format to succeed
        self.log.info("Perform dmg system erase on all system ranks:")
        self.get_dmg_command().system_erase()
        if self.get_dmg_command().result.exit_status != 0:
            self.fail("Issues performing system erase: {}".format(
                self.get_dmg_command().result.stderr_text))

        self.log.info("Perform dmg storage format on all system ranks:")

        # Calling storage format after system stop too soon would fail, so
        # wait 10 sec and retry up to 4 times.
        count = 0
        while count < 4:
            try:
                self.get_dmg_command().storage_format(force=True)
                if self.get_dmg_command().result.exit_status != 0:
                    self.fail(
                        "Issues performing storage format --force: {}".format(
                            self.get_dmg_command().result.stderr_text))
                break
            except CommandFailure as error:
                self.log.info(
                    "Storage format failed. Wait 10 sec and retry. %s", error)
                count += 1
                time.sleep(10)

        # Check that engine starts up again
        self.log.info("<SERVER> Waiting for the engines to start")
        self.server_managers[-1].detect_engine_start()

        # Check that we have cleared storage by checking pool list
        if self.get_dmg_command().get_pool_list_uuids():
            self.fail("Detected pools in storage after reformat: {}".format(
                self.get_dmg_command().result.stdout_text))

        # Create last pool now that memory has been wiped.
        self.add_pool_qty(quantity=1, connect=False)

        # Lastly, verify that last created pool is in the list
        pool_uuids = self.get_dmg_command().get_pool_list_uuids()
        self.assertEqual(pool_uuids[0].lower(), self.pool[-1].uuid.lower())
