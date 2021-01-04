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
import json

from daos_core_base import DaosCoreBase
from avocado.utils import process
from general_utils import get_log_file

class CSumErrorLog(DaosCoreBase):
    """
    Test Class Description: This test runs
    daos_test -z (Checksum tests) and verifies
    whether Checksum Error Counters are incremented
    in the NVME device due to checksum fault injection.
    :avocado: recursive
    """
    # pylint: disable=too-many-instance-attributes,too-many-ancestors
    def setUp(self):
        super(CSumErrorLog, self).setUp()
        self.dmg = self.get_dmg_command()
        self.dmg.hostlist = self.hostlist_servers[0]

    def get_nvme_device_id(self):
        """method to get nvme device-id. """
        self.dmg.json.value = True
        try:
            result = self.dmg.storage_query_list_devices()
        except process.CmdError as details:
            self.fail("dmg command failed: {}".format(details))

        data = json.loads(result.stdout)
        resp = data['response']
        if data['error'] or len(resp['host_errors']) > 0:
            if data['error']:
                self.fail("dmg command failed: {}".format(data['error']))
            else:
                self.fail("dmg command failed: {}".format(resp['host_errors']))
        for v in resp['host_storage_map'].values():
            if v['storage']['smd_info']['devices']:
                return v['storage']['smd_info']['devices'][0]['uuid']

    def get_checksum_error_value(self, device_id=None):
        """Get checksum error value from dmg storage_query_device_health.

        Args:
            device_id (str): Device UUID.
        """
        if device_id is None:
            self.fail("No device id provided")
            return
        self.dmg.json.value = True
        try:
            result = self.dmg.storage_query_device_health(device_id)
        except process.CmdError as details:
            self.fail("dmg command failed: {}".format(details))

        data = json.loads(result.stdout)
        resp = data['response']
        if data['error'] or len(resp['host_errors']) > 0:
            if data['error']:
                self.fail("dmg command failed: {}".format(data['error']))
            else:
                self.fail("dmg command failed: {}".format(resp['host_errors']))
        for v in resp['host_storage_map'].values():
            if v['storage']['smd_info']['devices']:
                dev = v['storage']['smd_info']['devices'][0]
                return dev['health']['checksum_errs']

    def test_csum_error_logging(self):
        """
        Test ID: DAOS-3927
        Test Description: Write Avocado Test to verify single data after
                          pool/container disconnect/reconnect.
        :avocado: tags=all,daily_regression,hw,medium,ib2,csum_error_log,faults
        """
        dev_id = self.get_nvme_device_id()
        self.log.info("%s", dev_id)
        csum = self.get_checksum_error_value(dev_id)
        self.dmg.copy_certificates(get_log_file("daosCA/certs"),
                                   self.hostlist_clients)
        self.log.info("Checksum Errors : %d", csum)
        DaosCoreBase.run_subtest(self)
        csum_latest = self.get_checksum_error_value(dev_id)
        self.log.info("Checksum Errors : %d", csum_latest)
        self.assertTrue(csum_latest > csum,
                        "Checksum Error Log not incremented")
        self.log.info("Checksum Error Logging Test Passed")
