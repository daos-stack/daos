#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


import time
from avocado.core.exceptions import TestFail
from pool_test_base import PoolTestBase
from command_utils_base import CommandFailure


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

        :avocado: tags=all,small,daily_regression,hw,control,sys_reformat,dmg
        """
        # Create pool using 90% of the available SCM capacity
        self.pool = self.get_pool_list(1, 0.9, None)
        self.pool[-1].create()

        self.log.info("Check that new pool will fail with DER_NOSPACE")
        self.get_dmg_command().exit_status_exception = False
        self.pool.extend(self.get_pool_list(1, 0.9, None))
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

        self.log.info("Stopping system prior to erase:")
        self.get_dmg_command().system_stop(force=True)
        if self.get_dmg_command().result.exit_status != 0:
            self.fail("Issues performing system stop --force: {}".format(
                self.get_dmg_command().result.stderr_text))

        self.log.info("Erasing system:")
        self.get_dmg_command().system_erase()
        if self.get_dmg_command().result.exit_status != 0:
            self.fail("Issues performing system erase: {}".format(
                self.get_dmg_command().result.stderr_text))

        self.log.info("Perform dmg storage format on all system ranks:")
        while True:
            try:
                self.get_dmg_command().storage_format(force=True)
                if self.get_dmg_command().result.exit_status == 0:
                    break
            except CommandFailure:
                time.sleep(0.5)

        # Check that engine starts up again
        self.log.info("<SERVER> Waiting for the engines to start")
        self.server_managers[0].detect_engine_start()

        # Check that we have cleared storage by checking pool list
        if self.get_dmg_command().pool_list():
            self.fail("Detected pools in storage after reformat: {}".format(
                self.get_dmg_command().result.stdout_text))

        # Create last pool now that memory has been wiped.
        self.pool.extend(self.get_pool_list(1, 0.9, None))
        self.pool[-1].create()

        # Lastly, verify that last created pool is in the list
        pool_info = self.get_dmg_command().pool_list()
        self.assertEqual(list(pool_info)[0], self.pool[-1].uuid)
