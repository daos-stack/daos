"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from avocado import fail_on

from daos_core_base import DaosCoreBase
from dmg_utils import get_dmg_smd_info
from exception_utils import CommandFailure
from general_utils import get_log_file


class CsumErrorLog(DaosCoreBase):
    """
    Test Class Description: This test runs
    daos_test -z (Checksum tests) and verifies
    whether Checksum Error Counters are incremented
    in the NVME device due to checksum fault injection.
    :avocado: recursive
    """
    # pylint: disable=too-many-instance-attributes

    @fail_on(CommandFailure)
    def get_checksum_error_value(self, dmg, device_id):
        """Get checksum error value from dmg storage_query_device_health.

        Args:
            dmg (DmgCommand): the DmgCommand object used to call storage_query_device_health()
            device_id (str): Device UUID.

        Returns:
            int: the number of checksum errors on the device
        """
        info = get_dmg_smd_info(dmg.storage_query_device_health, 'devices', uuid=device_id)
        for devices in info.values():
            for device in devices:
                try:
                    if device['uuid'] == device_id:
                        return device['health']['checksum_errs']
                except KeyError as error:
                    self.fail(
                        'Error parsing dmg storage query device-health output: {}'.format(error))
        return 0

    @fail_on(CommandFailure)
    def test_csum_error_logging(self):
        """Jira ID: DAOS-3927.

        Test Description: Write Avocado Test to verify single data after
                          pool/container disconnect/reconnect.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=checksum,faults,daos_test
        :avocado: tags=CsumErrorLog,test_csum_error_logging
        """
        self.log_step('Detecting server devices (dmg storage query list-devices)')
        test_run = False
        dmg = self.get_dmg_command()
        dmg.hostlist = self.hostlist_servers[0]
        host_devices = get_dmg_smd_info(dmg.storage_query_list_devices, 'devices')
        for host, devices in host_devices.items():
            for device in devices:
                for entry in ('uuid', 'tgt_ids', 'role_bits', 'roles'):
                    if entry not in device:
                        self.fail(
                            'Missing {} info from dmg storage query list devices'.format(entry))
                self.log.info(
                    'Host %s device: uuid=%s, targets=%s, role=%s, role_bits=%s',
                    host, device['uuid'], device['tgt_ids'], device['roles'], device['role_bits'])
                if not device['tgt_ids']:
                    self.log_step('Skipping device without targets on {}'.format(device['uuid']))
                    continue
                if device['roles'] and not int(device['role_bits']) & 1:
                    self.log_step(
                        'Skipping {} device without data on {}'.format(
                            device['role_bits'], device['uuid']))
                    continue
                if not device['uuid']:
                    self.fail('Device uuid undefined')
                self.log_step(
                    'Get checksum errors before running the test (dmg storage query device-health)')
                check_sum = self.get_checksum_error_value(dmg, device['uuid'])
                dmg.copy_certificates(get_log_file("daosCA/certs"), self.hostlist_clients)
                dmg.copy_configuration(self.hostlist_clients)
                self.log.info("Checksum Errors before: %d", check_sum)
                self.log_step('Run the test (daos_test -z)')
                self.run_subtest()
                test_run = True
                self.log_step(
                    'Get checksum errors after running the test (dmg storage query device-health)')
                check_sum_latest = self.get_checksum_error_value(dmg, device['uuid'])
                self.log.info('Checksum Errors after:  %d', check_sum_latest)
                self.assertTrue(check_sum_latest > check_sum, 'Checksum Error Log not incremented')
        if not test_run:
            self.fail('No tests run for the devices found')
        self.log_step('Test Passed')
