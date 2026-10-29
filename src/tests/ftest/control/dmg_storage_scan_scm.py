"""
  (C) Copyright 2020-2022 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from textwrap import wrap

from control_test_base import ControlTestBase
from run_utils import run_remote


class DmgStorageScanTest(ControlTestBase):
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

    def verify_storage_scan(self, storage_dict):
        """Verify SCM namespaces and NVMe devices returned by storage scan.

        Args:
            storage_dict (dict): Dictionary under "storage"

        Returns:
            list: List of errors.

        """
        errors = []
        scm_namespaces = storage_dict.get("scm_namespaces") or []
        nvme_devices = storage_dict.get("nvme_devices") or []

        if not scm_namespaces and not nvme_devices:
            errors.append("No SCM or NVMe storage found!")

        for scm_namespace in scm_namespaces:
            # Verify that all namespaces exist under /dev.
            pmem_name = scm_namespace["blockdev"]
            ls_cmd = f"ls {os.path.join('/dev', pmem_name)}"
            if not run_remote(self.log, self.hostlist_servers, ls_cmd).passed:
                errors.append(f"{pmem_name} didn't exist under /dev!")

            # Verify the Socket ID.
            numa_node_path = os.path.join(
                os.sep, "sys", "class", "block", pmem_name, "device", "numa_node")
            command = f"cat {numa_node_path}"
            result = run_remote(self.log, self.hostlist_servers, command)
            if not result.passed:
                errors.append(f"{command} failed on {result.failed_hosts}")
                continue
            expected_numa_node = result.joined_stdout
            actual_numa_node = str(scm_namespace["numa_node"])

            if expected_numa_node != actual_numa_node:
                msg = "Unexpected Socket ID! Expected: {}, Actual: {}".format(
                    expected_numa_node, actual_numa_node)
                errors.append(msg)

        for nvme_device in nvme_devices:
            pci_addr = nvme_device["pci_addr"]
            pci_addr_values = pci_addr.split(":")
            numa_node_path = os.path.join(os.sep, "sys", "class", "pci_bus")

            if len(pci_addr_values[0]) == 6:
                # VMD controller address
                pci_addr_parts = wrap(pci_addr_values[0], 2)
                pci_addr_base = ":".join(["0000"] + pci_addr_parts[0:1])
                pci_addr_head = ".".join(
                    [":".join(["0000"] + pci_addr_parts[0:2]), str(int(pci_addr_parts[2]))])
                numa_node_path = os.path.join(
                    numa_node_path, pci_addr_base, "device", pci_addr_head, "numa_node")
            else:
                pci_addr_head = ":".join(pci_addr_values[0:2])
                numa_node_path = os.path.join(
                    numa_node_path, pci_addr_head, "device", "numa_node")

            command = f"cat {numa_node_path}"
            result = run_remote(self.log, self.hostlist_servers, command)
            if not result.passed:
                errors.append(f"{command} failed on {result.failed_hosts}")
                continue

            expected_numa_node = str(nvme_device["socket_id"])
            actual_numa_node = result.joined_stdout
            if expected_numa_node != actual_numa_node:
                msg = "Unexpected NVMe Socket ID! Expected: {}, Actual: {}".format(
                    expected_numa_node, actual_numa_node)
                errors.append(msg)

        return errors

    def test_dmg_storage_scan(self):
        """
        JIRA ID: DAOS-1507

        Test Description: Test dmg storage scan --verbose

        1. Verify SCM namespace devices exist in /dev and match their NUMA node.
        2. Verify NVMe devices match their PCI NUMA node.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,storage_scan,scm
        :avocado: tags=DmgStorageScanTest,test_dmg_storage_scan
        """
        self.verify_dmg_storage_scan(self.verify_storage_scan)
