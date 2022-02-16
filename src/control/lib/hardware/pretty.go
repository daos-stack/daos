//
// (C) Copyright 2021 Intel Corporation.
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

	for _, numaNode := range t.NUMANodes {
		coreSet := &system.RankSet{}
		for _, core := range numaNode.Cores {
			coreSet.Add(system.Rank(core.ID))
		}

		fmt.Fprintf(ew, "NUMA Node %d\n", numaNode.ID)
		fmt.Fprintf(ew, "  CPU cores: %s\n", coreSet)
		fmt.Fprintf(ew, "  PCI devices:\n")
		for addr, devs := range numaNode.PCIDevices {
			fmt.Fprintf(ew, "    %s\n", &addr)
			for _, dev := range devs {
				fmt.Fprintf(ew, "      %s\n", dev)
			}
		}
		fmt.Fprintf(ew, "  PCI buses:\n")
		for _, bus := range numaNode.PCIBuses {
			fmt.Fprintf(ew, "    %s\n", bus)
		}
	}

	return ew.Err
}
