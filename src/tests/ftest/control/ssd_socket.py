#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from general_utils import pcmd, run_task


class SSDSocketTest(TestWithServers):
    """Test Class Description: Verify NVMe NUMA socket values.

    This test covers the requirement SRS-10-0034.
    dmg supports listing of available storage (NVDIMMs, SSD) and netwok adapters
    and in all cases shows socket affinity

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
            task = run_task(
                hosts=self.hostlist_servers,
                command="find /sys -name \"{}\"".format(pci_addr_head))
            for output, _ in task.iter_buffers():
                self.log.debug(output)

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

    def test_scan_ssd(self):
        """
        JIRA ID: DAOS-3584

        Test Description: Verify NVMe NUMA socket values.

        :avocado: tags=all,small,full_regression,hw,control,ssd_socket
        """
        # Call dmg storage scan --verbose and get the PCI addresses.
        data = self.get_dmg_command().storage_scan(verbose=True)
        pci_addrs = data[self.hostlist_servers[0]]["nvme"].keys()
        self.log.info("Testing PCI addresses: %s", pci_addrs)

        pci_addr_heads = []
        errors = []

        # For every PCI address, verify its Socket ID against its NUMA socket
        # ID.
        for pci_addr in pci_addrs:
            # Get the PCI Address Head and construct the path to numa_node.
            cmd_socket_id = data[self.hostlist_servers[0]]["nvme"][pci_addr]\
                ["socket"]
            pci_addr_values = pci_addr.split(":")
            pci_addr_head = "{}:{}".format(
                pci_addr_values[0], pci_addr_values[1])
            pci_addr_heads.append(pci_addr_head)
            numa_node_path = "/sys/class/pci_bus/{}/device/numa_node".format(
                pci_addr_head)

            # Call cat on the server host, not necessarily the local test host.
            task = run_task(
                hosts=[self.hostlist_servers[0]],
                command="cat {}".format(numa_node_path))

            # Obtain the numa_node content.
            fs_socket_id = ""
            for output, _ in task.iter_buffers():
                fs_socket_id = str(output).splitlines()[-1]

            # Test that the content is expected.
            if fs_socket_id != cmd_socket_id:
                errors.append(
                    "Unexpected socket ID! Cmd: {}; FS: {}".format(
                        cmd_socket_id, fs_socket_id))

        if errors:
            # Since we're dealing with system files and we don't have access to
            # them in CI, we need some debugging info when the test fails to
            # better understand the result.
            self.debug_numa_node(pci_addr_heads)
            self.fail("Error found!\n{}".format("\n".join(errors)))
