#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from general_utils import pcmd, run_pcmd
from control_test_base import ControlTestBase


class SSDSocketTest(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description: Verify NVMe NUMA socket values.

    This test covers the requirement SRS-10-0034.
    dmg supports listing of available storage (NVDIMMs, SSD) and network
    adapters and in all cases shows socket affinity

    Call dmg storage scan --verbose to obtain NUMA socket value (Socket ID) of
    each NVMe disk. Verify against the value in
    /sys/class/pci_bus/<PCI Address Head>/device/numa_node

    where PCI Address Head is the first two hex numbers separated by colon.
    e.g., 0000:5e:00.0 -> PCI Address Head is 0000:5e

    :avocado: recursive
    """

    def debug_numa_node(self, pci_addr_heads):
        """Debug numa_node file by searching it in /sys and call hwloc-ls.

        Args:
            pci_addr_heads (list): List of PCI address head.
        """
        for pci_addr_head in pci_addr_heads:
            self.log.debug(
                "----- Search PCI Addr Head %s in /sys -----", pci_addr_head)
            run_pcmd(
                hosts=self.hostlist_servers,
                command="find /sys -name \"{}\"".format(pci_addr_head),
                verbose=True)

        # Another way to obtain the Socket ID is to use hwloc-ls --whole-io
        # --verbose. It contains something like:

        # Bridge Host->PCI L#9 (P#2 buses=0000:[80-81])
        #     Bridge PCI->PCI (P#524320 busid=0000:80:02.0 id=8086:2f04
        #     class=0604(PCI_B) buses=0000:[81-81])
        #         PCI 8086:2701 (P#528384 busid=0000:81:00.0 class=0108(NVMExp)
        #         PCISlot=801)

        # In this case, the PCI address was 0000:81:00.0. We can figure out
        # which NUMA node section these lines are in. This approach is clearly
        # much more cumbersome than reading the numa_node, so it's called here
        # for mainly debugging purpose.
        self.log.debug("----- Show PCI Address in hwloc-ls -----")
        pcmd(
            hosts=self.hostlist_servers,
            command="hwloc-ls --whole-io --verbose")

    def verify_ssd_sockets(self, storage_dict):
        """Main test component.

        Args:
            storage_dict (dict): Dictionary under "storage"

        Returns:
            list: List of errors.
        """
        nvme_devices = storage_dict["nvme_devices"]

        pci_addr_heads = []
        errors = []

        # For every PCI address, verify its Socket ID against its NUMA socket
        # ID.
        for nvme_device in nvme_devices:
            cmd_socket_id = nvme_device["socket_id"]

            # Get the PCI Address Head and construct the path to numa_node.
            pci_addr = nvme_device["pci_addr"]
            pci_addr_values = pci_addr.split(":")
            pci_addr_head = "{}:{}".format(
                pci_addr_values[0], pci_addr_values[1])
            pci_addr_heads.append(pci_addr_head)
            numa_node_path = "/sys/class/pci_bus/{}/device/numa_node".format(
                pci_addr_head)

            # Call cat on the server host, not necessarily the local test host.
            results = run_pcmd(
                hosts=self.hostlist_servers[0:1],
                command="cat {}".format(numa_node_path))

            # Obtain the numa_node content.
            fs_socket_id = ""
            for result in results:
                # Test that the content is expected.
                fs_socket_id = result["stdout"][-1]
                if fs_socket_id != str(cmd_socket_id):
                    errors.append(
                        "Unexpected socket ID! Cmd: {}; FS: {}".format(
                            cmd_socket_id, fs_socket_id))

        if errors:
            # Since we're dealing with system files and we don't have access to
            # them in CI, we need some debugging info when the test fails to
            # better understand the result.
            self.debug_numa_node(pci_addr_heads)

        return errors

    def test_scan_ssd(self):
        """
        JIRA ID: DAOS-3584

        Test Description: Verify NVMe NUMA socket values.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,ssd_socket
        """
        self.verify_dmg_storage_scan(self.verify_ssd_sockets)
