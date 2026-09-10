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
from ior_utils import write_data
from run_utils import run_remote
from test_utils_pool import add_pool, get_size_params


class DmgSystemReformatTest(TestWithServers):
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
        :avocado: tags=hw,medium
        :avocado: tags=control,dmg,system_reformat
        :avocado: tags=DmgSystemReformatTest,test_dmg_system_reformat
        """
        dmg = self.get_dmg_command().copy()

        self.log_step("Create pool using 90% of the available NVMe capacity")
        pool = add_pool(self, connect=False, dmg=dmg)

        self.log_step("Write data to all targets in the pool")
        container = self.get_container(pool)
        write_data(self, container)

        self.log_step("Check that new pool will fail with DER_NOSPACE")
        with dmg.no_exception():
            pool2 = add_pool(self, connect=False, create=False, **get_size_params(pool))
            try:
                pool2.create()
                self.fail("Pool create was expected to fail with DER_NOSPACE!")
            except TestFail as error:
                self.log.info("Pool create failed: %s", str(error))
                if "-1007" not in str(error):
                    self.fail("Pool create did not fail due to DER_NOSPACE!")

        self.log_step("Verify expected metadata exists before erase")
        # Check this upfront so later when we check it is gone we know the erase worked
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        superblock_path = os.path.join(scm_mount, "superblock")
        control_metadata_path = self.server_managers[0].manager.job.control_metadata.path.value
        run_remote(self.log, self.hostlist_servers, f"sudo ls -l {scm_mount}")
        _, missing_hosts = check_file_exists(self.hostlist_servers, superblock_path, sudo=True)
        if missing_hosts:
            self.fail(f"superblock does not exist before erase: {superblock_path}")
        if control_metadata_path:
            self.log.info("control_metadata_path = '%s'", control_metadata_path)
            _, missing_hosts = check_file_exists(
                self.hostlist_servers, control_metadata_path, sudo=True)
            if missing_hosts:
                self.fail(f"control metadata does not exist before erase: {control_metadata_path}")

        self.log_step("Stop running engine instances: 'dmg system stop'")
        dmg.system_stop(force=True)

        # Perform a dmg system erase to allow the dmg storage format to succeed
        self.log_step("Perform dmg system erase on all system ranks:")
        dmg.system_erase()

        # Disable pool and container cleanup in teardown since this pool was erased
        pool.skip_cleanup()
        pool = None
        container.skip_cleanup()
        container = None

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

        self.log_step("Verify no pools are in the pool list after reformat")
        if dmg.get_pool_list_uuids():
            self.fail("Detected pools in storage after reformat: {}".format(
                dmg.result.stdout_text))

        self.log_step("Create a new pool and verify only the new pool is in the pool list")
        pool = add_pool(self, connect=False, dmg=dmg)
        pool_uuids = list(map(str.lower, dmg.get_pool_list_uuids()))
        self.assertEqual(
            pool_uuids, [pool.uuid.lower()], f"unexpected pool list output: {pool_uuids}")
