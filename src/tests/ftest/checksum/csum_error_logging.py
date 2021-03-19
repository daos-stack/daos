#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
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
        super().setUp()
        self.dmg = self.get_dmg_command()
        self.dmg.hostlist = self.hostlist_servers[0]

    def get_nvme_device_id(self):
        """method to get nvme device-id. """
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
        uuid = None
        for value in list(resp['host_storage_map'].values()):
            if value['storage']['smd_info']['devices']:
                uuid = value['storage']['smd_info']['devices'][0]['uuid']
                break
        return uuid

    def get_checksum_error_value(self, device_id=None):
        """Get checksum error value from dmg storage_query_device_health.

        Args:
            device_id (str): Device UUID.
        """
        if device_id is None:
            self.fail("No device id provided")

        self.dmg.json.value = True
        try:
            result = self.dmg.storage_query_device_health(device_id)
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
        checksum_errs = None
        for value in list(resp['host_storage_map'].values()):
            if value['storage']['smd_info']['devices']:
                dev = value['storage']['smd_info']['devices'][0]
                checksum_errs = dev['health']['checksum_errs']
                break
        return checksum_errs

    def test_csum_error_logging(self):
        """Jira ID: DAOS-3927

        Test Description: Write Avocado Test to verify single data after
                          pool/container disconnect/reconnect.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=faults
        :avocado: tags=csum_error_log
        """
        dev_id = self.get_nvme_device_id()
        self.log.info("%s", dev_id)
        csum = self.get_checksum_error_value(dev_id)
        self.dmg.copy_certificates(get_log_file("daosCA/certs"),
                                   self.hostlist_clients)
        self.dmg.copy_configuration(self.hostlist_clients)
        self.log.info("Checksum Errors : %d", csum)
        self.run_subtest()
        csum_latest = self.get_checksum_error_value(dev_id)
        self.log.info("Checksum Errors : %d", csum_latest)
        self.assertTrue(csum_latest > csum,
                        "Checksum Error Log not incremented")
        self.log.info("Checksum Error Logging Test Passed")
