#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from general_utils import pcmd
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

        Test Description: Test dmg storage scan with and without --verbose.

        :avocado: tags=all,small,full_regression,hw,control,dmg_storage_scan_scm
        """
        # Use --verbose and obtain the SCM Namespace values such as pmem0,
        # pmem1.
        data = self.get_dmg_command().storage_scan(verbose=True)
        host = self.hostlist_servers[0]
        pmem_names = data[host]["scm"].keys()
        # Verifies that all namespaces exist under /dev.
        RC_SUCCESS = 0
        for pmem_name in pmem_names:
            lscmd = "{} {}".format("ls", os.path.join("/dev", pmem_name))
            # rc is a dictionary where return code is the key.
            rc = pcmd(hosts=self.hostlist_servers, command=lscmd)
            self.assertTrue(RC_SUCCESS in rc)

        # Call without verbose and verify the namespace value.
        data = self.get_dmg_command().storage_scan()
        self.assertEqual(
            data[host]["scm"]["details"],
            "{} namespaces".format(len(pmem_names)))
