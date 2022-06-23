#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from general_utils import pcmd, run_pcmd
from control_test_base import ControlTestBase


class DmgStorageScanSCMTest(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    This test partially covers the following requirement.
    (TR-1.0.34) admin can use daos_shell to collect information and create yaml
    file by himself. This means that daos_shell allows to list:
    SCM module and NVMe SSDs with NUMA affinity
    network adapter with NUMA affinity

    This test focuses on the correctness of SCM info obtained by dmg storage
    scan (so that the admin can create a yaml file correctly). First, it
    verifies the SCM Namespaces exist in /dev. Second, it verifies the namespace
    count by comparing against the number of namespace rows obtained with
    --verbose.
    :avocado: recursive
    """
    def verify_storage_scam_scm(self, storage_dict):
        """Main test component.

        Args:
            storage_dict (dict): Dictionary under "storage"

        Returns:
            list: List of errors.
        """
        errors = []
        RC_SUCCESS = 0

        for scm_namespace in storage_dict["scm_namespaces"]:
            # Verify that all namespaces exist under /dev.
            pmem_name = scm_namespace["blockdev"]
            lscmd = "{} {}".format("ls", os.path.join("/dev", pmem_name))
            # rc is a dictionary where return code is the key.
            rc = pcmd(hosts=self.hostlist_servers, command=lscmd)

            if RC_SUCCESS not in rc:
                errors.append("{} didn't exist under /dev!".format(pmem_name))

            # Verify the Socket ID.
            numa_node_path = "/sys/class/block/{}/device/numa_node".format(
                pmem_name)
            command = "cat {}".format(numa_node_path)
            out_list = run_pcmd(hosts=self.hostlist_servers, command=command)

            # This one is in str.
            expected_numa_node = out_list[0]["stdout"][0]
            actual_numa_node = str(scm_namespace["numa_node"])

            if expected_numa_node != actual_numa_node:
                msg = "Unexpected Socket ID! Expected: {}, Actual: {}".format(
                    expected_numa_node, actual_numa_node)
                errors.append(msg)

        return errors

    def test_dmg_storage_scan_scm(self):
        """
        JIRA ID: DAOS-1507

        Test Description: Test dmg storage scan --verbose

        1. Verify the device name such as pmem0 exists in /dev
        2. Verify the Socket ID matches with the one in
        /sys/class/block/<dev_name>/device/numa_node

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,dmg_storage_scan_scm
        """
        self.verify_dmg_storage_scan(self.verify_storage_scam_scm)
