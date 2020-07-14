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
        pmem_names = data["scm_namespaces"]
        # Verifies that all namespaces exist under /dev.
        RC_SUCCESS = 0
        for pmem_name in pmem_names:
            lscmd = "{} {}".format("ls", os.path.join("/dev", pmem_name))
            # rc is a dictionary where return code is the key.
            rc = pcmd(hosts=self.hostlist_servers, command=lscmd)
            self.assertTrue(RC_SUCCESS in rc)

        # Call without verbose and verify the namespace value.
        data = self.get_dmg_command().storage_scan()
        self.assertEqual(int(data["namespaces"]), len(pmem_names))
