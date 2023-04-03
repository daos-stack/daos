"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re

import avocado

from control_test_base import ControlTestBase
from dmg_utils import get_storage_query_pool_info, get_storage_query_device_info
from exception_utils import CommandFailure
from general_utils import list_to_str, dict_to_str


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
        self.targets = self.server_managers[-1].get_config_value('targets')
        md_on_ssd = False

        self.log_step('Determining server storage config')
        bdev_tiers = 0
        self.bdev_list = []
        for engine in self.server_managers[-1].manager.job.yaml.engine_params:
            for index, tier in enumerate(engine.storage.storage_tiers):
                if tier.storage_class.value == 'ram':
                    md_on_ssd = True
                elif tier.storage_class.value == 'nvme':
                    bdev_tiers += 1
                    for device in tier.bdev_list.value:
                        self.bdev_list.append(
                            {'bdev': device, 'roles': tier.roles.value, 'tier': index})
        if md_on_ssd:
            for device in self.bdev_list:
                if device['roles']:
                    continue
                if bdev_tiers == 1 and device['tier'] == 1:
                    # First bdev storage tier of 1 tier: all roles
                    device['roles'] = 'wal,data,meta'
                elif bdev_tiers == 2 and device['tier'] == 1:
                    # First bdev storage tier of 2 tiers: wal roles
                    device['roles'] = 'wal'
                elif bdev_tiers == 2 and device['tier'] == 2:
                    # Second bdev storage tier of 2 tiers: data & meta roles
                    device['roles'] = 'data,meta'
                elif bdev_tiers > 2 and device['tier'] == 1:
                    # First bdev storage tier of >2 tiers: wal
                    device['roles'] = 'wal'
                elif bdev_tiers > 2 and device['tier'] == 2:
                    # Second bdev storage tier of >2 tiers: meta
                    device['roles'] = 'meta'
                else:
                    # Additional bdev storage tier of >2 tiers: data
                    device['roles'] = 'data'
        self.log.info('Detected NVMe devices in config')
        for bdev in self.bdev_list:
            self.log.info('  %s', dict_to_str(bdev, items_joiner=':'))

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
        JIRA ID: DAOS-3925, DAOS-13011

        Test Description: Test 'dmg storage query list-devices' command.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,dmg,storage_query,basic
        :avocado: tags=DmgStorageQuery,test_dmg_storage_query_devices
        """
        # Get the storage device information, parse and check devices info
        self.log_step('Get the storage device information')
        device_info = get_storage_query_device_info(self, self.dmg)

        # Check if the number of devices match the config
        self.log_step('Verify storage device count')
        if len(self.bdev_list) != len(device_info):
            self.fail(
                'Number of devices ({}) do not match server config ({})'.format(
                    len(device_info), len(self.bdev_list)))

        # Check that number of targets match the config
        self.log_step('Verify storage device targets and roles')
        errors = 0
        targets = 0
        for device in device_info:
            self.log.info('Verifying device %s', device['tr_addr'])
            targets += len(device['tgt_ids'])
            self.log.info('  targets: detected=%s', len(device['tgt_ids']))
            message = ''
            for bdev in self.bdev_list:
                bdev_tr_addr = '{:02x}{:02x}{:02x}:'.format(
                    *list(map(int, re.split(r'[:.]', bdev['bdev'])[1:], [16] * 3)))
                if device['tr_addr'] == bdev['bdev'] or device['tr_addr'].startswith(bdev_tr_addr):
                    message = 'detected={}, expected={}'.format(device['roles'], bdev['roles'])
                    if device['roles'] != bdev['roles']:
                        message += ' <= ERROR unexpected role'
                        errors += 1
            if not message:
                message = 'detected={}, expected=*NO MATCH* <= ERROR unexpected role'.format(
                    device['roles'])
                errors += 1
            self.log.info('  roles:   %s', message)

        if self.targets != targets:
            self.fail('Wrong number of targets found: {}'.format(targets))
        if errors:
            self.fail('Errors detected verifying roles: {}'.format(errors))
        self.log.info('Test passed')

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
