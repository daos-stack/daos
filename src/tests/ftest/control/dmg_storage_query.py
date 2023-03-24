"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import avocado

from control_test_base import ControlTestBase
from dmg_utils import get_storage_query_pool_info, get_storage_query_device_info
from exception_utils import CommandFailure
from general_utils import list_to_str


class DmgStorageQuery(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:

    Test to verify dmg storage health query commands and device state commands.
    Including: storage query, storage blobstore-health,
    storage query device-state.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for dmg storage query."""
        super().setUp()
        self.bdev_list = self.server_managers[-1].get_config_value("bdev_list")
        self.targets = self.server_managers[-1].get_config_value("targets")

    def check_dev_state(self, device_info, state):
        """Check the state of the device.

        Args:
            device_info (list): list of device information.
            state (str): device state to verify.
        """
        errors = 0
        for device in device_info:
            if device['dev_state'] != state:
                self.log.info(
                    "Device %s not found in the %s state: %s",
                    device['uuid'], state, device['dev_state'])
                errors += 1
        if errors:
            self.fail("Found {} device(s) not in the {} state".format(errors, state))

    def test_dmg_storage_query_devices(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query list-devices' command.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,dmg,storage_query,basic
        :avocado: tags=DmgStorageQuery,test_dmg_storage_query_devices
        """
        # Get the storage device information, parse and check devices info
        device_info = get_storage_query_device_info(self, self.dmg)

        # Check if the number of devices match the config
        msg = "Number of devs do not match cfg: {}".format(len(self.bdev_list))
        self.assertEqual(len(self.bdev_list), len(device_info), msg)

        # Check that number of targets match the config
        targets = 0
        for device in device_info:
            targets += len(device['tgt_ids'])
        if self.targets != targets:
            self.fail("Wrong number of targets found: {}".format(targets))

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_pools(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query list-pools' command.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,dmg,storage_query,basic
        :avocado: tags=DmgStorageQuery,test_dmg_storage_query_pools
        """
        # Create pool and get the storage smd information, then verify info
        self.prepare_pool()
        pool_info = get_storage_query_pool_info(self, self.dmg, verbose=True)

        # Check the dmg storage query list-pools output for inaccuracies
        errors = 0
        for pool in pool_info:
            # Check pool uuid
            if pool['uuid'].lower() != self.pool.uuid.lower():
                self.log.info(
                    "Incorrect pool UUID for %s: detected=%s", str(self.pool), pool['uuid'])
                errors += 1
                continue
            if self.targets != len(pool['tgt_ids']):
                self.log.info(
                    "Incorrect number of targets for %s: detected=%s, expected=%s",
                    str(self.pool), len(pool['tgt_ids']), self.targets)
                errors += 1
            if self.targets != len(pool['blobs']):
                self.log.info(
                    "Incorrect number of blobs for %s: detected=%s, expected=%s",
                    str(self.pool), len(pool['blobs']), self.targets)
                errors += 1
        if errors:
            self.fail(
                "Detected {} problem(s) with the dmg storage query list-pools output".format(
                    errors))

        # Destroy pool and get pool information and check there is no pool
        self.pool.destroy()
        if get_storage_query_pool_info(self, self.dmg, verbose=True):
            self.fail(
                "Pool info detected in dmg storage query list-pools output after pool destroy")

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_device_health(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query list-devices --health' cmd.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,dmg,storage_query,basic
        :avocado: tags=DmgStorageQuery,test_dmg_storage_query_device_health
        """
        errors = []
        device_info = get_storage_query_device_info(self, self.dmg, health=True)
        for device in device_info:
            self.log.info("Health Info for %s:", device['uuid'])
            for key in sorted(device['health']):
                if key == 'temperature':
                    self.log.info("  %s: %s", key, device['health'][key])
                    # Verify temperature, convert from Kelvins to Celsius
                    celsius = int(device['health'][key]) - 273.15
                    if not 0.00 <= celsius <= 71.00:
                        self.log.info("    Out of range (0-71 C) temperature detected: %s", celsius)
                        errors.append(key)
                elif key == 'temp_warn':
                    self.log.info("  %s: %s", key, device['health'][key])
                    if device['health'][key]:
                        self.log.info("    Temperature warning detected: %s", device['health'][key])
                        errors.append(key)
                elif 'temp_time' in key:
                    self.log.info("  %s: %s", key, device['health'][key])
                    if device['health'][key] != 0:
                        self.log.info(
                            "    Temperature time issue detected: %s", device['health'][key])
                        errors.append(key)
        if errors:
            self.fail("Temperature error detected on SSDs: {}".format(list_to_str(errors)))

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_device_state(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query list-devices' command.

        In addition this test also does a basic test of nvme-faulty cmd:
        'dmg storage set nvme-faulty'

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,dmg,storage_query,basic
        :avocado: tags=DmgStorageQuery,test_dmg_storage_query_device_state
        """
        # Get device info and check state is NORMAL
        device_info = get_storage_query_device_info(self, self.dmg)
        self.check_dev_state(device_info, "NORMAL")

        # Set device to faulty state and check that it's in FAULTY state
        for device in device_info:
            try:
                self.dmg.storage_set_faulty(uuid=device['uuid'])
            except CommandFailure:
                self.fail("Error setting the faulty state for {}".format(device['uuid']))

        # Check that devices are in FAULTY state
        device_info = get_storage_query_device_info(self, self.dmg)
        self.check_dev_state(device_info, "EVICTED")
