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

    def check_dev_state(self, state, devs_info):
        """Check the state of the devices.

        Args:
            devs_info (list): device info containing lists with
                [UUID, VOS tgt IDs, blobs].
        """
        status_err = []
        for dev in devs_info:
            state_info = self.get_dmg_output(
                "storage_query_device_state", devuuid=dev[0])
            if state_info[1] != state:
                status_err.append(":".join(state_info))
        if status_err:
            msg = "Found device in bad state: {}".format(status_err)
            self.assertEqual(len(status_err), 0, msg)

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_smd_devices(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query smd -d' command.

        :avocado: tags=all,pr,hw,small,storage_query,smd_devs,basic
        """
        # Get the storage smd information, parse and check devices info
        devs_info = self.get_smd_info(devices=True)

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
        pools_info = self.get_smd_info(pools=True)

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
        devs_info = self.get_smd_info(devices=True)

        # Get the blobstore info from dmg cmd
        blob_info = []
        e_blob_info = self.params.get("blobstore_info", "/run/*")
        for dev in devs_info:
            blob_info.append(
                self.get_dmg_output("storage_query_blobstore", devuuid=dev[0]))

        # Check that we have expected number of devices
        msg = "Found wrong number of devices in blobstore info"
        self.assertEqual(len(e_blob_info), len(blob_info), msg)

        # Verify temperature, convert from Kelvins to Celsius
        temp_err = []
        for info in blob_info:
            cels_temp = int(info[6]) - 273.15
            if cels_temp not in range(0, 71):
                temp_err.append("{}: {}".format(info[0], cels_temp))
            info.pop(6)
        if temp_err:
            msg = "Bad temperature on SSDs: {}".format(",".join(temp_err))
            self.assertEqual(len(temp_err), 0, msg)

        # Compare the rest of the values in blob info
        err = []
        for dmg_info, exp_info in zip(blob_info, e_blob_info):
            if dmg_info[1:] != exp_info:
                err.append("dmg info :{} != expected info:{}".format(
                    dmg_info, exp_info))
        if err:
            self.assertEqual(len(err), 0, "Blob info not as expected")

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_device_state(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query device-state' command.
        In addition this test also does a basic test of nvme-faulty cmd:
        'dmg storage query nvme-faulty'

        :avocado: tags=all,pr,hw,small,storage_query,device_state,basic
        """
        # Get device info and check state is NORMAL
        devs_info = self.get_smd_info(devices=True)
        self.check_dev_state("NORMAL", devs_info)

        # Set device to faulty state and check that it's in FAULTY state
        for dev in devs_info:
            self.get_dmg_output("storage_set_faulty", devuuid=dev[0])

        # Check that devices are in FAULTY state
        self.check_dev_state("FAULTY", devs_info)
