"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from daos_core_base import DaosCoreBase
from dmg_utils import get_storage_query_device_uuids, get_storage_query_smd_info
from general_utils import get_log_file


class CsumErrorLog(DaosCoreBase):
    """
    Test Class Description: This test runs
    daos_test -z (Checksum tests) and verifies
    whether Checksum Error Counters are incremented
    in the NVME device due to checksum fault injection.
    :avocado: recursive
    """
    # pylint: disable=too-many-instance-attributes,too-many-ancestors

    def get_checksum_error_value(self, dmg, device_id):
        """Get checksum error value from dmg storage_query_device_health.

        Args:
            dmg (DmgCommand): the DmgCommand object used to call storage_query_device_health()
            device_id (str): Device UUID.

        Returns:
            int: the number of checksum errors on the device
        """
        checksum_errs = 0
        response = get_storage_query_smd_info(self, dmg.storage_query_device_health, uuid=device_id)
        for smd_info in response.values():
            try:
                for device in smd_info['devices']:
                    if device['uuid'] == device_id:
                        checksum_errs = device['health']['checksum_errs']
                        break
            except KeyError as error:
                self.fail('Error parsing dmg storage query device-health output: {}'.format(error))
        return checksum_errs

    def test_csum_error_logging(self):
        """Jira ID: DAOS-3927

        Test Description: Write Avocado Test to verify single data after
                          pool/container disconnect/reconnect.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=checksum,faults
        :avocado: tags=CsumErrorLog,test_csum_error_logging
        """
        dmg = self.get_dmg_command()
        dmg.hostlist = self.hostlist_servers[0]
        dev_uuids = get_storage_query_device_uuids(self, dmg)
        self.log.info("dev_uuids: %s", dev_uuids)
        for host_uuid_list in dev_uuids.values():
            for device_id in host_uuid_list:
                if device_id is None:
                    self.fail("Device id undefined")
                self.log.info("dev_id: %s", device_id)
                csum = self.get_checksum_error_value(dmg, device_id)
                dmg.copy_certificates(get_log_file("daosCA/certs"), self.hostlist_clients)
                dmg.copy_configuration(self.hostlist_clients)
                self.log.info("Checksum Errors before: %d", csum)
                self.run_subtest()
                csum_latest = self.get_checksum_error_value(dmg, device_id)
                self.log.info("Checksum Errors after:  %d", csum_latest)
                self.assertTrue(csum_latest > csum, "Checksum Error Log not incremented")
        self.log.info("Checksum Error Logging Test Passed")
