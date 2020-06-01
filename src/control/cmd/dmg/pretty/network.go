//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package pretty

import (
	"fmt"
	"io"
	"strings"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

const (
	providerTitle  = "Provider"
	interfaceTitle = "Interfaces"
	socketTitle    = "NUMA Socket"
)

// PrintHostFabricMap generates a human-readable representation of the supplied
// HostFabricMap and writes it to the supplied io.Writer.
func PrintHostFabricMap(hfm control.HostFabricMap, out io.Writer, onlyProviders bool, opts ...control.PrintConfigOption) error {
	if len(hfm) == 0 {
		return nil
	}

	ew := txtfmt.NewErrWriter(out)

	for _, key := range hfm.Keys() {
		var hfiMap map[uint32]map[string][]string

		hfs := hfm[key]
		hosts := control.GetPrintHosts(hfs.HostSet.RangedString(), opts...)
		lineBreak := strings.Repeat("-", len(hosts))
		iw := txtfmt.NewIndentWriter(ew, txtfmt.WithPadCount(4))
		fmt.Fprintf(ew, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)
		fmt.Fprintln(ew)
		fmt.Fprintf(iw, "Available providers: %s\n", strings.Join(hfs.HostFabric.Providers, ", "))
		fmt.Fprintln(ew)

		if onlyProviders {
			continue
		}

		hfiMap = make(map[uint32]map[string][]string)
		for _, fi := range hfs.HostFabric.Interfaces {
			if _, ok := hfiMap[fi.NumaNode]; !ok {
				hfiMap[fi.NumaNode] = make(map[string][]string)
			}
			hfiMap[fi.NumaNode][fi.Provider] = append(hfiMap[fi.NumaNode][fi.Provider], fi.Device)
		}

		for _, hfi := range hfiMap {
			for _, devices := range hfi {
				devices = control.DedupeStringSlice(devices)
			}
		}

		iwTable := txtfmt.NewIndentWriter(iw, txtfmt.WithPadCount(4))
		for s, hfi := range hfiMap {
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

			fmt.Fprintf(iwTable, fmt.Sprintf("%s", formatter.Format(table)))
			fmt.Fprintln(ew)
		}
	}

	return ew.Err
}
