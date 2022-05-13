//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"fmt"
	"io"

	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/system"
)

// PrintTopology prints the topology to the given writer.
func PrintTopology(t *Topology, output io.Writer) error {
	ew := txtfmt.NewErrWriter(output)

	if t == nil {
		fmt.Fprintf(ew, "No topology information available\n")
		return nil
	}

	for _, numaNode := range t.NUMANodes.AsSlice() {
		coreSet := &system.RankSet{}
		for _, core := range numaNode.Cores {
			coreSet.Add(system.Rank(core.ID))
		}

		fmt.Fprintf(ew, "NUMA Node %d\n", numaNode.ID)
		fmt.Fprintf(ew, "  CPU cores: %s\n", coreSet)
		fmt.Fprintf(ew, "  PCI buses:\n")
		for _, bus := range numaNode.PCIBuses {
			fmt.Fprintf(ew, "    %s\n", bus)
			for _, addr := range numaNode.PCIDevices.Keys() {
				if !bus.Contains(addr) {
					continue
				}

				fmt.Fprintf(ew, "      %s\n", addr)
				for _, dev := range numaNode.PCIDevices.Get(addr) {
					fmt.Fprintf(ew, "        %s\n", dev)
				}
			}
		}

	}

	if len(t.VirtualDevices) > 0 {
		fmt.Fprintln(ew, "Virtual Devices")

		for _, dev := range t.VirtualDevices {
			fmt.Fprintf(ew, "  %s (%s)\n", dev.Name, dev.Type)
			if dev.BackingDevice != nil {
				fmt.Fprintf(ew, "    backed by: %s\n", dev.BackingDevice)
			}
		}
	}

	return ew.Err
}
