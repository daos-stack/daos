"""
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from exception_utils import CommandFailure
from general_utils import check_file_exists, journalctl_time
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

        self.log_step("Create pool using 90% of the available NVMe capacity")
        pool = add_pool(self, dmg=dmg)

        self.log_step("Check that new pool will fail with DER_NOSPACE")
        with dmg.no_exception():
            pool2 = add_pool(self, create=False, **get_size_params(pool))
            try:
                pool2.create()
                self.fail("Pool create was expected to fail with DER_NOSPACE!")
            except TestFail as error:
                self.log.info("Pool create failed: %s", str(error))
                if "-1007" not in str(error):
                    self.fail("Pool create did not fail due to DER_NOSPACE!")

        self.log_step("Stop running engine instances: 'dmg system stop'")
        dmg.system_stop(force=True)

        self.log_step("Verify expected metadata exists before erase")
        # Check this upfront so later when we check it is gone we know the erase worked
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        superblock_path = os.path.join(scm_mount, "superblock")
        control_metadata_path = self.server_managers[0].manager.job.control_metadata.path.value
        _, missing_hosts = check_file_exists(self.hostlist_servers, superblock_path, sudo=True)
        if missing_hosts:
            self.fail(f"superblock does not exist before erase: {superblock_path}")
        if control_metadata_path:
            self.log.info("control_metadata_path = '%s'", control_metadata_path)
            _, missing_hosts = check_file_exists(
                self.hostlist_servers, control_metadata_path, sudo=True)
            if missing_hosts:
                self.fail(f"control metadata does not exist before erase: {control_metadata_path}")

        # Perform a dmg system erase to allow the dmg storage format to succeed
        self.log_step("Perform dmg system erase on all system ranks:")
        dmg.system_erase()

        # Disable pool cleanup in teardown since this pool was erased
        pool.skip_cleanup()
        pool = None

        self.log_step("Verify metadata is erased")
        _, missing_hosts = check_file_exists(self.hostlist_servers, superblock_path, sudo=True)
        if missing_hosts != self.hostlist_servers:
            self.fail(f"superblock still exists after erase: {superblock_path}")
        if control_metadata_path:
            _, missing_hosts = check_file_exists(
                self.hostlist_servers, control_metadata_path, sudo=True)
            if missing_hosts != self.hostlist_servers:
                self.fail(f"control metadata still exists after erase: {control_metadata_path}")

        self.log_step("Perform dmg storage format on all system ranks:")
        # Calling storage format after system stop too soon would fail, so
        # wait 10 sec and retry up to 4 times.
        for _ in range(4):
            try:
                dmg.storage_format(force=True)
                break
            except CommandFailure as error:
                self.log.info("Storage format failed. Wait 10 sec and retry. %s", error)
                time.sleep(10)

        self.log_step("Verify engines start again")
        self.log.info("<SERVER> Waiting for the engines to start")
        self.server_managers[-1].manager.timestamps["start"] = journalctl_time()
        self.server_managers[-1].detect_engine_start()

        # Check that we have cleared storage by checking pool list
        if dmg.get_pool_list_uuids():
            self.fail("Detected pools in storage after reformat: {}".format(
                dmg.result.stdout_text))

        # Create last pool now that memory has been wiped.
        pool = add_pool(self, connect=False, dmg=dmg)

        # Lastly, verify that last created pool is in the list
        pool_uuids = dmg.get_pool_list_uuids()
        self.assertEqual(pool_uuids[0].lower(), pool.uuid.lower(), f"{pool} missing from list")
