"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from avocado import fail_on
from dmg_utils import get_storage_query_device_uuids
from exception_utils import CommandFailure
from nvme_utils import set_device_faulty
from osa_utils import OSAUtils


class VmdLedStatus(OSAUtils):
    """
    Test Class Description: This class methods to get
    the VMD LED status.

    :avocado: recursive
    """
    # pylint: disable=too-many-instance-attributes,too-many-ancestors
    def setUp(self):
        super().setUp()
        self.dmg = self.get_dmg_command()
        self.dmg.hostlist = self.hostlist_servers[0]

    def run_vmd_led_identify(self, device_id=None):
        """Run the VMD LED identify command.

        Args:
            device_id (str, optional): Device UUID. Defaults to None.
        Returns:
            dmg LED identify command response.
        """
        if device_id is None:
            self.fail("No device id provided")

        try:
            result = self.dmg.storage_led_identify(ids=device_id)
        except CommandFailure as details:
            self.fail("dmg command failed: {}".format(details))

        self.log.info(result)
        if result['error']:
            self.fail("dmg command failed: {}".format(result['error']))
        elif len(result['response']['host_errors']) > 0:
            self.fail("dmg command failed: {}".format(result['response']['host_errors']))
        return result

    def get_led_status_value(self, device_id=None):
        """Get the LED status value (VMD or NVME)

        Args:
            device_id (str, optional): Device UUID. Defaults to None.
        Returns:
            Get the LED status value.
        """
        if device_id is None:
            self.fail("No device id provided")

        try:
            result = self.dmg.storage_led_check(ids=device_id)
        except CommandFailure as details:
            self.fail("dmg command failed: {}".format(details))

        self.log.info(result)
        if result['error'] or len(result['response']['host_errors']) > 0:
            if result['error']:
                self.fail("dmg command failed: {}".format(result['error']))
            else:
                self.fail("dmg command failed: {}".format(result['response']['host_errors']))
        return result

    @fail_on(CommandFailure)
    def test_vmd_led_status(self):
        """Jira ID: DAOS-11290

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=vmd,vmd_led
        :avocado: tags=VmdLedStatus,test_vmd_led_status
        """
        host_uuids = get_storage_query_device_uuids(self.dmg)
        for hosts, uuid_list in host_uuids.items():
            self.log.info("Devices on hosts %s: %s", hosts, uuid_list)
            for uuid in uuid_list:
                led_identify_result = self.run_vmd_led_identify(uuid)
                get_led_result = self.get_led_status_value(uuid)
                self.log.info("Sleeping for 15 seconds ...")
                time.sleep(15)
                self.log.info(led_identify_result)
                self.log.info(get_led_result)

    @fail_on(CommandFailure)
    def test_vmd_led_faulty(self):
        """Jira ID: DAOS-11290

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=vmd,vmd_led
        :avocado: tags=VmdLedStatus,test_vmd_led_faulty
        """
        host_uuids = get_storage_query_device_uuids(self.dmg)
        for hosts, uuid_list in host_uuids.items():
            self.log.info("Devices on hosts %s: %s", hosts, uuid_list)
            for uuid in uuid_list:
                resp = set_device_faulty(self, self.dmg, hosts.split(':')[0], uuid)
                self.log.info("Sleeping for 15 seconds ...")
                time.sleep(15)
                self.log.info(resp)

    @fail_on(CommandFailure)
    def test_disk_failure_recover(self):
        """Jira ID: DAOS-11284

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=vmd,vmd_led
        :avocado: tags=VmdLedStatus,test_disk_failure_recover
        """
        host_uuids = get_storage_query_device_uuids(self.dmg)
        for hosts, uuid_list in host_uuids.items():
            self.log.info("Devices on hosts %s: %s", hosts, uuid_list)
            self.log.info("First device on hosts %s: %s", hosts, uuid_list[0])
            resp = set_device_faulty(self, self.dmg, hosts.split(':')[0], uuid_list[0])
            self.log.info("Sleeping for 15 seconds ...")
            time.sleep(15)
            self.log.info(resp)
            resp = self.dmg.storage_replace_nvme(old_uuid=uuid_list[0], new_uuid=uuid_list[0])
            self.log.info("Sleeping for 60 seconds ...")
            time.sleep(60)
            self.log.info(resp)
