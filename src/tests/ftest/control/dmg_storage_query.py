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
import re
from command_utils import CommandFailure
from control_test_base import ControlTestBase


class DmgStorageQuery(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:

    Test to verify dmg storage health query commands and device state commands.
    Including: storage query, storage blobstore-health,
    storage query device-state.

    :avocado: recursive
    """

    def setUp(self):
        "Set up for dmg storage query."
        super(DmgStorageQuery, self).setUp()
        self.bdev_list = self.server_managers[-1].get_config_value("bdev_list")
        self.targets = self.server_managers[-1].get_config_value("targets")

    def check_dev_state(self, devs_info, state):
        """Check the state of the device.

        Args:
            devs_info (list): list of device information.
            state (str): device state to verify.
        """
        err = []
        for dev in devs_info.values()[0]:
            if dev[4] != state:
                err.append(dev)
        if err:
            self.fail("Found device(s) in bad state: {}".format(err))

    def test_dmg_storage_query_devices(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query list-devices' command.

        :avocado: tags=all,pr,hw,small,storage_query_devs,basic,dmg
        """
        # Get the storage device information, parse and check devices info
        devs_info = self.get_device_info()

        # Check if the number of devices match the config
        msg = "Number of devs do not match cfg: {}".format(len(self.bdev_list))
        self.assertEqual(len(self.bdev_list), len(devs_info.values()[0]), msg)

        # Check that number of targets match the config
        targets = 0
        for devs in devs_info.values()[0]:
            targets += len(devs[2].split(" "))
        if self.targets != targets:
            self.fail("Wrong number of targets found: {}".format(targets))

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_pools(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query list-pools' command.

        :avocado: tags=all,pr,hw,small,storage_query_pools,basic,dmg
        """
        # Create pool and get the storage smd information, then verfify info
        self.prepare_pool()
        pools_info = self.get_pool_info(verbose=True)

        # Check pool uuid
        for pool in pools_info.values()[0]:
            self.assertEqual(self.pool.pool.get_uuid_str(), pool[0].upper())

        # Check that number of pool blobs match the number of targets
        t_err = []
        b_err = []
        for pool in pools_info.values()[0]:
            vos_targets = pool[2].split()
            blobs = pool[3].split()
            if self.targets != len(vos_targets):
                t_err.append(pool[0])
            if self.targets != len(blobs):
                b_err.append(pool[0])

        self.assertEqual(len(t_err), 0, "Wrong number of targets in pool info")
        self.assertEqual(len(b_err), 0, "Wrong number of blobs in pool info")

        # Destroy pool and get pool information and check there is no pool
        self.pool.destroy()
        no_pool_info = self.get_pool_info()
        self.assertFalse(no_pool_info, "No pools should be detected.")

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_device_health(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query list-devices --health' cmd.

        :avocado: tags=all,pr,hw,small,storage_query_health,basic
        """
        dmg_info = self.get_device_info(health=True)

        # Cleanup output
        if dmg_info:
            for idx, info in enumerate(dmg_info):
                dmg_info[idx] = [i for i in info if i]
        parsed = [dmg_info[i:(i + 19)] for i in range(0, len(dmg_info), 19)]
        _ = parsed[0].pop(0)

        # Convert from list of lists to list of strings
        health_info = []
        for i in parsed:
            h = [elem[0] for elem in i]
            health_info.append(h)

        self.log.info("Found health info: %s", str(health_info))

        # Verify temperature, convert from Kelvins to Celsius
        temp_err = []
        for info in health_info:
            cels_temp = int("".join(re.findall(r"\d+", info[2]))) - 273.15
            if not 0.00 <= cels_temp <= 71.00:
                temp_err.append("{}".format(cels_temp))
        if temp_err:
            self.fail("Bad temperature on SSDs: {}".format(",".join(temp_err)))

    @avocado.fail_on(CommandFailure)
    def test_dmg_storage_query_device_state(self):
        """
        JIRA ID: DAOS-3925

        Test Description: Test 'dmg storage query device-state' command.
        In addition this test also does a basic test of nvme-faulty cmd:
        'dmg storage set nvme-faulty'

        :avocado: tags=all,pr,hw,small,storage_query_faulty,basic
        """
        # Get device info and check state is NORMAL
        devs_info = self.get_device_info()
        self.check_dev_state(devs_info, "NORMAL")

        # Set device to faulty state and check that it's in FAULTY state
        for dev in devs_info.values()[0]:
            self.get_dmg_output("storage_set_faulty", uuid=dev[0])

        # Check that devices are in FAULTY state
        self.check_dev_state(self.get_device_info(), "EVICTED")
