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

type hfiMap map[uint32]map[string][]string

func (h hfiMap) addInterface(fi *control.HostFabricInterface) {
	if _, ok := h[fi.NumaNode]; !ok {
		h[fi.NumaNode] = make(map[string][]string)
	}
	h[fi.NumaNode][fi.Provider] = append(h[fi.NumaNode][fi.Provider], fi.Device)
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

			fmt.Fprintf(iwTable, fmt.Sprintf("%s", formatter.Format(table)))
			fmt.Fprintln(ew)
		}
	}

	return ew.Err
}
