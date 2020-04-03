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

from apricot import TestWithServers
from test_utils_pool import TestPool
from command_utils import CommandFailure
from control_test_base import ControlTestBase


class DmgStorageQuery(ControlTestBase):
    """Test Class Description:

    Test to verify dmg storage health query commands and device state commands.
    Including: storage query, storage blobstore-health, storage nvme-health,
    storage query device-state.

    :avocado: recursive
    """
    def check_len(self, attr_name, expected_len):
        """Check if VOS ids provided by dmg match config value.

        Args:
            attr_name (str): Name of config attribute wanted.
            expected_len (int): Len of the list that contains the config_attr.
        """
        params = self.server_managers[-1].runner.job.yaml_params
        cfg_attr = getattr(params.server_params[-1], attr_name)
        if expected_len != cfg_attr.value:
            self.fail("Number of {}: {} dont match config val: {}".format(
                attr_name, expected_len, cfg_attr.value))
        return True

    def check_smd_out(self, smd_info, valid_info, invalid_info):
        """Check the smd output with specified

        Args:
            valid_info (str): Info from "devices" or "pools"
            invalid_info (str): Info from "devices" or "pools"
        """
        if smd_info:
            if valid_info in smd_info:
                self.log.info("Found uuid: %s", smd_info[valid_info]["uuid"])
                self.check_len("targets", len(smd_info[valid_info]["vos_ids"]))
                # Perform additional check for pools uuid and blobs
                if valid_info == "pools":
                    if smd_info[valid_info]["uuid"] != self.pool.uuid:
                        self.fail("Pool uuids don't match: {} {}".format(
                            smd_info[valid_info]["uuid"], self.pool.uuid))
                    self.check_len(
                        "targets", len(smd_info[valid_info]["spdk_blobs"]))
            else:
                self.fail("Wanted information not found: {}".format(smd_info))

            if invalid_info in smd_info:
                self.fail("Found unwanted pool information: {}".format(
                    smd_info[invalid_info]))
        else:
            self.fail("SMD info not found: {}".format(smd_info))

    def test_dmg_storage_query_smd_devices(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query smd -d' command.

        :avocado: tags=all,pr,hw,small,dmg_storage_query,smd_devs,basic
        """
        # Get the storage smd infromation, parse and check devices info
        smd_info = self.get_dmg_cmd_info("storage_query_smd", pools=False)
        self.check_smd_out(smd_info, "devices", "pools")

    def test_dmg_storage_query_smd_pools(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query smd -p' command.

        :avocado: tags=all,pr,hw,small,dmg_storage_query,smd_pools,basic
        """
        # Create pool and get the storage smd information, then verfify info
        self.prepare_pool(self.dmg)
        smd_info = self.get_dmg_cmd_info("storage_query_smd", devices=False)
        self.check_smd_out(smd_info, "pools", "devices")

        # Destroy pool and get the storage smd information, then verify info
        self.pool.destroy()
        smd_info = self.get_dmg_cmd_info("storage_query_smd", devices=False)
        self.check_smd_out(smd_info, "devices", "pools")

    def test_dmg_storage_query_blobstore_health(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query blobstore-health' command.

        :avocado: tags=all,pr,hw,small,dmg_storage_query,blobstore,basic
        """
        # Create pool and get the storage smd information
        self.prepare_pool(self.dmg)
        smd_info = self.get_dmg_cmd_info("storage_query_smd", devices=False)

        # Parse stdout into a dict check that devices information is displayed.
        blobstore_info = []
        for info in smd_info:
            if "Device" in info:
                uuid = info[info.index("Device") + 1]
                blobstore_info.append(
                    self.get_dmg_cmd_info("storage_query_blobstore", uuid))

        # Compare config expected values with dmg output
        e_blob_info = self.params.get("blobstore_info", "/run/*")
        if blobstore_info.sort() != e_blob_info.sort():
            self.fail("dmg storage query expected output: {}".format(
                e_blob_info))

    def test_dmg_storage_query_device_state(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query device-state' command.

        :avocado: tags=all,pr,hw,small,dmg_storage_query,device_state,basic
        """
        smd_info = self.get_dmg_cmd_info("storage_query_smd", devices=False)

        # Parse stdout into a dict check that devices information is displayed.
        device_state_info = []
        for info in smd_info:
            if "Device" in info:
                uuid = info[info.index("Device") + 1]
                device_state_info.append(
                    self.get_dmg_cmd_info("storage_query_device_state", uuid))

        # Check if the number of devices match the config
        if self.check_len("dbev_list", len(device_state_info)):
            # Check that the state of each device is NORMAL
            for dev in device_state_info:
                if dev[2] == "NORMAL":
                    self.fail("Found a device in bad state: {}".format(dev[2]))
