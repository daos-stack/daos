#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from general_utils import pcmd, run_pcmd
from apricot import TestWithServers


class DmgStorageScanSCMTest(TestWithServers):
    """Test Class Description:
    This test partially covers the following requirement.
    (TR-1.0.34) admin can use daos_shell to collect information and create yaml
    file by himself. This means that daos_shell allows to list:
    SCM module and NVMe SSDs with NUMA affinity
    network adaptor with NUMA affinity

    This test focuses on the correctness of SCM info obtained by dmg storage
    scan (so that the admin can create a yaml file correctly). First, it
    verifies the SCM Namespaces exist in /dev. Second, it verifies the namespace
    count by comparing against the number of namespace rows obtained with
    --verbose.
    :avocado: recursive
    """

    def test_dmg_storage_scan_scm(self):
        """
        JIRA ID: DAOS-1507

        Test Description: Test dmg storage scan --verbose

        1. Verify the device name such as pmem0 exists in /dev
        2. Verify the Socket ID matches with the one in
        /sys/class/block/<dev_name>/device/numa_node

        :avocado: tags=all,small,full_regression,hw,control,dmg_storage_scan_scm
        """
        # Use --verbose and obtain the SCM Namespace values such as pmem0,
        # pmem1.
        data = self.get_dmg_command().storage_scan(verbose=True)

        temp_dict = data["response"]["HostStorage"]
        struct_hash = list(temp_dict.keys())[0]
        scm_namespaces = temp_dict[struct_hash]["storage"]["scm_namespaces"]

        RC_SUCCESS = 0
        for scm_namespace in scm_namespaces:
            # Verify that all namespaces exist under /dev.
            pmem_name = scm_namespace["blockdev"]
            lscmd = "{} {}".format("ls", os.path.join("/dev", pmem_name))
            # rc is a dictionary where return code is the key.
            rc = pcmd(hosts=self.hostlist_servers, command=lscmd)
            self.assertTrue(RC_SUCCESS in rc)

            # Verify the Socket ID.
            numa_node_path = "/sys/class/block/{}/device/numa_node".format(
                pmem_name)
            command = "cat {}".format(numa_node_path)
            out_list = run_pcmd(hosts=self.hostlist_servers, command=command)

            # This one is in str.
            expected_numa_node = out_list[0]["stdout"][0]
            actual_numa_node = str(scm_namespace["numa_node"])

            msg = "Unexpected Socket ID! Expected: {}, Actual: {}".format(
                expected_numa_node, actual_numa_node)
            self.assertEqual(expected_numa_node, actual_numa_node, msg)
