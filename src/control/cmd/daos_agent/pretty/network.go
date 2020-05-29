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

	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

const (
	providerTitle  = "Provider"
	interfaceTitle = "Interfaces"
	socketTitle    = "NUMA Socket"
)

// dedupeStringSlice is responsible for returning a slice based on
// the input with any duplicates removed.
func dedupeStringSlice(in []string) []string {
	keys := make(map[string]struct{})

	for _, el := range in {
		keys[el] = struct{}{}
	}

	out := make([]string, 0, len(keys))
	for key := range keys {
		out = append(out, key)
	}

	return out
}

// PrintFabricScan generates a human-readable representation of the supplied
// FabricScan results and writes it to the supplied io.Writer.
func PrintFabricScan(fs []netdetect.FabricScan, out io.Writer) error {
	if len(fs) == 0 {
		return nil
	}
	var hfiMap map[uint]map[string][]string

	ew := txtfmt.NewErrWriter(out)
	iw := txtfmt.NewIndentWriter(ew, txtfmt.WithPadCount(4))

	hfiMap = make(map[uint]map[string][]string)
	for _, fi := range fs {
		if _, ok := hfiMap[fi.NUMANode]; !ok {
			hfiMap[fi.NUMANode] = make(map[string][]string)
		}
		hfiMap[fi.NUMANode][fi.Provider] = append(hfiMap[fi.NUMANode][fi.Provider], fi.DeviceName)
	}

	for _, hfi := range hfiMap {
		for _, devices := range hfi {
			devices = dedupeStringSlice(devices)
		}
	}

	for s, hfi := range hfiMap {
		var table []txtfmt.TableRow

		formatter := txtfmt.NewTableFormatter(providerTitle, interfaceTitle)
		title := fmt.Sprintf("%s %d", socketTitle, s)
		lineBreak := strings.Repeat("-", len(title))

		fmt.Fprintf(ew, "%s\n%s\n%s\n", lineBreak, title, lineBreak)
		fmt.Fprintln(ew)

		for p, dev := range hfi {
			row := txtfmt.TableRow{providerTitle: p, interfaceTitle: strings.Join(dev, ", ")}
			table = append(table, row)
		}

		fmt.Fprintf(iw, fmt.Sprintf("%s", formatter.Format(table)))
		fmt.Fprintln(ew)
	}
	return ew.Err
}
