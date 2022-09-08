#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import json
import time
from osa_utils import OSAUtils
from avocado.utils import process

class VMDLEDStatus(OSAUtils):
    """
    Test Class Description: This class methods to get
    the VMD LED status.
    """
    # pylint: disable=too-many-instance-attributes,too-many-ancestors
    def setUp(self):
        super().setUp()
        self.targets = self.params.get("targets", "/run/server_config/servers/0/*")
        self.dmg = self.get_dmg_command()
        self.dmg.hostlist = self.hostlist_servers[0]

    def get_nvme_device_ids(self):
        """Get the list of nvme device-ids.
        Returns: List of uuid
        """
        self.dmg.json.value = True
        try:
            result = self.dmg.storage_query_list_devices()
        except process.CmdError as details:
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
        uuid = []
        for value in list(resp['host_storage_map'].values()):
            if value['storage']['smd_info']['devices']:
                for target in range(self.targets):
                    uuid.append(value['storage']['smd_info']['devices'][target]['uuid'])
        return uuid

    def get_LED_status_value(self, device_id=None):
        """Get LED Status value.

        Args:
            device_id (str): Device UUID
        Returns:
            dmg LED status information
        """
        if device_id is None:
            self.fail("No device id provided")

        self.dmg.json.value = True
        try:
            result = self.dmg.storage_identify_device(uuid=device_id)
        except process.CmdError as details:
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

    def set_device_faulty(self, device_id=None):
        """Get a device to faulty state.

        Args:
            device_id (str): Device UUID
        Returns:
            dmg device faulty information.
        """
        if device_id is None:
            self.fail("No device id provided")

        self.dmg.json.value = True
        try:
            result = self.dmg.storage_set_faulty(uuid=device_id)
        except process.CmdError as details:
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


    def test_VMD_LED_Status(self):
        """Jira ID: DAOS-11290

        :avocado: tags=all,manual
        :avocado: tags=hw,medium,ib2
        :avocado: tags=vmd_led,faults
        :avocado: tags=vmd_led_basic
        """
        dev_id = []
        # Get the list of device ids.
        dev_id = self.get_nvme_device_ids()
        self.log.info("%s", dev_id)
        for val in dev_id:
            resp = self.get_LED_status_value(val)
            time.sleep(15)
            self.log.info(resp)

    def test_VMD_LED_Faulty(self):
        """Jira ID: DAOS-11290

        :avocado: tags=all,manual
        :avocado: tags=hw,medium,ib2
        :avocado: tags=vmd_led,faults
        :avocado: tags=vmd_led_fault
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

        :avocado: tags=all,manual
        :avocado: tags=hw,medium,ib2
        :avocado: tags=vmd_led,faults
        :avocado: tags=vmd_disk_failure_recover
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
                resp = self.storage_replace_device(old_uuid=val, new_uuid=val)
                time.sleep(60)
                self.log.info(resp)
            count = count + 1
