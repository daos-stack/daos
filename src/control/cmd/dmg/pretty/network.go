//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"
	"strings"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

type hfiMap map[uint]map[string][]string

func (h hfiMap) addInterface(fi *control.HostFabricInterface) {
	nn := uint(fi.NumaNode)
	if _, ok := h[nn]; !ok {
		h[nn] = make(map[string][]string)
	}
	h[nn][fi.Provider] = append(h[nn][fi.Provider], fi.Device)
}

// PrintHostFabricMap generates a human-readable representation of the supplied
// HostFabricMap and writes it to the supplied io.Writer.
func PrintHostFabricMap(hfm control.HostFabricMap, out io.Writer, opts ...PrintConfigOption) error {
	if len(hfm) == 0 {
		return nil
	}

	ew := txtfmt.NewErrWriter(out)

	providerTitle := "Provider"
	interfaceTitle := "Interfaces"
	socketTitle := "NUMA Socket"

	for _, key := range hfm.Keys() {
		hfs := hfm[key]
		hosts := getPrintHosts(hfs.HostSet.RangedString(), opts...)
		lineBreak := strings.Repeat("-", len(hosts))
		iw := txtfmt.NewIndentWriter(ew, txtfmt.WithPadCount(4))
		fmt.Fprintf(ew, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)
		fmt.Fprintln(ew)

		hfim := make(hfiMap)
		for _, fi := range hfs.HostFabric.Interfaces {
			hfim.addInterface(fi)
		}

		iwTable := txtfmt.NewIndentWriter(iw, txtfmt.WithPadCount(4))
		for s, hfi := range hfim {
			var table []txtfmt.TableRow

			formatter := txtfmt.NewTableFormatter(providerTitle, interfaceTitle)
			title := fmt.Sprintf("%s %d", socketTitle, s)
			lineBreak := strings.Repeat("-", len(title))

			fmt.Fprintf(iw, "%s\n%s\n%s\n", lineBreak, title, lineBreak)
			fmt.Fprintln(ew)

			for p, dev := range hfi {
				row := txtfmt.TableRow{providerTitle: p, interfaceTitle: strings.Join(dev, ", ")}
				table = append(table, row)
			}

			fmt.Fprint(iwTable, formatter.Format(table))
			fmt.Fprintln(ew)
		}
	}

	return ew.Err
}
