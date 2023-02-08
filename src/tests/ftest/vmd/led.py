"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import json
import time
from osa_utils import OSAUtils
from exception_utils import CommandFailure


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

    def get_nvme_device_ids(self):
        """Get the list of nvme device-ids.
        Returns:
            list: List of UUID
        """
        self.dmg.json.value = True
        try:
            result = self.dmg.storage_query_list_devices()
        except CommandFailure as details:
            self.fail("dmg command failed: {}".format(details))

        data = json.loads(result.stdout_text)
        resp = data['response']
        if data['error'] or len(resp['host_errors']) > 0:
            if data['error']:
                self.fail("dmg command failed: {}".format(data['error']))
            else:
                self.fail("dmg command failed: {}".format(resp['host_errors']))
        uuid = []
        for value in list(resp['host_storage_map'].values()):
            if value['storage']['smd_info']['devices']:
                for device in value['storage']['smd_info']['devices']:
                    uuid.append(device['uuid'])
        return uuid

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

    def set_device_faulty(self, device_id=None):
        """Get a device to faulty state.

        Args:
            device_id (str, optional): Device UUID. Defaults to None.
        Returns:
            dict: dmg device faulty information.
        """
        if device_id is None:
            self.fail("No device id provided")

        self.dmg.json.value = True
        try:
            result = self.dmg.storage_set_faulty(uuid=device_id)
        except CommandFailure as details:
            self.fail("dmg command failed: {}".format(details))
        finally:
            self.dmg.json.value = False

        data = json.loads(result.stdout_text)
        resp = data['response']
        if data['error'] or len(resp['host_errors']) > 0:
            if data['error']:
                self.fail("dmg command failed: {}".format(data['error']))
            else:
                self.fail("dmg command failed: {}".format(resp['host_errors']))
        return resp

    def test_vmd_led_status(self):
        """Jira ID: DAOS-11290

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=vmd,vmd_led
        :avocado: tags=VmdLedStatus,test_vmd_led_status
        """
        dev_id = []
        # Get the list of device ids.
        dev_id = self.get_nvme_device_ids()
        self.log.info("%s", dev_id)
        for val in dev_id:
            led_identify_result = self.run_vmd_led_identify(val)
            get_led_result = self.get_led_status_value(val)
            time.sleep(15)
            self.log.info(led_identify_result)
            self.log.info(get_led_result)

    def test_vmd_led_faulty(self):
        """Jira ID: DAOS-11290

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=vmd,vmd_led
        :avocado: tags=VmdLedStatus,test_vmd_led_faulty
        """
        dev_id = []
        # Get the list of device ids.
        dev_id = self.get_nvme_device_ids()
        self.log.info("%s", dev_id)
        for val in dev_id:
            resp = self.set_device_faulty(val)
            time.sleep(15)
            self.log.info(resp)

    def test_disk_failure_recover(self):
        """Jira ID: DAOS-11284

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=vmd,vmd_led
        :avocado: tags=VmdLedStatus,test_disk_failure_recover
        """
        dev_id = []
        # Get the list of device ids.
        dev_id = self.get_nvme_device_ids()
        self.log.info("%s", dev_id)
        count = 0
        for val in dev_id:
            if count == 0:
                resp = self.set_device_faulty(val)
                time.sleep(15)
                self.log.info(resp)
                resp = self.dmg.storage_replace_nvme(old_uuid=val, new_uuid=val)
                time.sleep(60)
                self.log.info(resp)
            count = count + 1
