#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

import avocado
from command_utils import CommandFailure
from control_test_base import ControlTestBase


class DmgStorageQuery(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:

    Test to verify dmg storage health query commands and device state commands.
    Including: storage query, storage blobstore-health, storage nvme-health,
    storage query device-state.

    :avocado: recursive
    """

    def setUp(self):
        "Set up for dmg storage query."
        super(DmgStorageQuery, self).setUp()
        self.bdev_list = self.server_managers[-1].get_config_value("bdev_list")
        self.targets = self.server_managers[-1].get_config_value("targets")

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_smd_devices(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query smd -d' command.

        :avocado: tags=all,pr,hw,small,storage_query,smd_devs,basic
        """
        # Get the storage smd infromation, parse and check devices info
        smd_info = self.get_dmg_output("storage_query_smd", devices=True)
        devs_info = [smd_info[i:(i + 2)] for i in range(0, len(smd_info), 2)]

        # Check if the number of devices match the config
        msg = "Number of devs doesn't match cfg: {}".format(len(self.bdev_list))
        self.assertEqual(len(self.bdev_list), len(devs_info), msg)

        # Check that number of targets match the config
        errors = []
        for devs in devs_info:
            devs[1] = devs[1].split()
            if self.targets != len(devs[1]):
                errors.append(devs[0])

        msg = "Found wrong number of targets in device info"
        self.assertEqual(len(errors), 0, msg)

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_smd_pools(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query smd -p' command.

        :avocado: tags=all,pr,hw,small,storage_query,smd_pools,basic
        """
        # Create pool and get the storage smd information, then verfify info
        self.prepare_pool()
        smd_info = self.get_dmg_output("storage_query_smd", pools=True)
        pools_info = [smd_info[i:(i + 2)] for i in range(0, len(smd_info), 2)]

        # Check pool uuid
        for pool in pools_info:
            self.assertEqual(self.pool.pool.get_uuid_str(), pool[0])

        # Destroy pool and get smd information and check there is no pool
        self.pool.destroy()
        smd_info = self.get_dmg_output("storage_query_smd", pools=True)
        self.assertFalse(smd_info)

        # Check that number of pool blobs match the number of targets
        t_err = []
        b_err = []
        for pool in pools_info:
            vos_targets = pool[1].split()
            blobs = pool[2].split()
            if self.targets != len(vos_targets):
                t_err.append(pool[0])
            if self.targets != len(blobs):
                b_err.append(pool[0])

        self.assertEqual(len(t_err), 0, "Wrong number of targets in pool info")
        self.assertEqual(len(b_err), 0, "Wrong number of blobs in pool info")

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_blobstore_health(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query blobstore-health' command.

        :avocado: tags=all,pr,hw,small,storage_query,blobstore,basic
        """
        smd_info = self.get_dmg_output("storage_query_smd", devices=False)
        devs_info = [smd_info[i:(i + 2)] for i in range(0, len(smd_info), 2)]

        # Get the device uuid and run command
        blob_info = []
        for dev in devs_info:
            blob_info.append(
                self.get_dmg_output("storage_query_blobstore", devuuid=dev[0]))

        # Compare config expected values with dmg output
        e_blob_info = self.params.get("blobstore_info", "/run/*")
        e_blob_info.insert(0, dev[0])

        #

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_device_state(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query device-state' command.

        :avocado: tags=all,pr,hw,small,storage_query,device_state,basic
        """
        smd_info = self.get_dmg_output("storage_query_smd", devices=True)
        devs_info = [smd_info[i:(i + 2)] for i in range(0, len(smd_info), 2)]

        # Check that the state of each device is NORMAL
        status_err = []
        for dev in devs_info:
            status = self.get_dmg_output(
                "storage_query_device_state", devuuid=dev[0])[1])
            if status != "NORMAL":
                status_err.append([dev[0], status])



        # Set device to faulty state and check that it's in FAULTY state
        devs_info = self.get_devs_info(smd_info, "storage_set_faulty")
        for dev in devs_info:
            if dev[2] != "FAULTY":
                self.fail("Found a device in {} state.".format(dev[2]))

        devs_info = self.get_devs_info(smd_info, "storage_query_device_state")
        for dev in devs_info:
            if dev[2] != "FAULTY":
                self.fail("Found a device in {} state.".format(dev[2]))

    def test_dmg_storage_set_nvme_fault(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query nvme-faulty' command.

        :avocado: tags=all,pr,hw,small,storage_query,nvme_faulty,basic
        """
        # Set nvme-faulty command to run without devuuid provided.
        self.dmg.set_sub_command("storage")
        self.dmg.sub_command_class.set_sub_command("set")
        self.dmg.sub_command_class.sub_command_class. \
            set_sub_command("nvme-faulty")

        # Run command, expected to fail.
        try:
            self.dmg.run()
        except CommandFailure as error:
            self.log.info("Command failed as expected:%s", error)
