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
// +build firmware

package pretty

import (
	"fmt"
	"io"
	"strings"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

func getPrintVersion(version string) string {
	if version == "" {
		return "N/A"
	}
	return version
}

// PrintSCMFirmwareQueryMap formats the firmware query results for human readability.
func PrintSCMFirmwareQueryMap(fwMap control.HostSCMQueryMap, out io.Writer,
	opts ...control.PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	for _, host := range fwMap.Keys() {
		fwResults := fwMap[host]
		lineBreak := strings.Repeat("-", len(host))
		fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, host, lineBreak)

		iw := txtfmt.NewIndentWriter(out)
		if len(fwResults) == 0 {
			fmt.Fprintln(iw, "No SCM devices detected")
			continue
		}

		for _, res := range fwResults {
			err := printScmModule(&res.Module, iw)
			if err != nil {
				return err
			}

			iw1 := txtfmt.NewIndentWriter(iw)

			if res.Error != nil {
				fmt.Fprintf(iw1, "Error querying firmware: %s\n", res.Error.Error())
				continue
			}

			if res.Info == nil {
				fmt.Fprint(iw1, "Error: No information available\n")
				continue
			}

			fmt.Fprintf(iw1, "Active Version: %s\n", getPrintVersion(res.Info.ActiveVersion))
			fmt.Fprintf(iw1, "Staged Version: %s\n", getPrintVersion(res.Info.StagedVersion))
			fmt.Fprintf(iw1, "Maximum Firmware Image Size: %s\n", humanize.IBytes(uint64(res.Info.ImageMaxSizeBytes)))
			fmt.Fprintf(iw1, "Update Status: %s\n", res.Info.UpdateStatus)
		}
	}

	return w.Err
}

// PrintSCMFirmwareUpdateMap formats the firmware query results for human readability.
func PrintSCMFirmwareUpdateMap(fwMap control.HostSCMUpdateMap, out io.Writer,
	opts ...control.PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	for _, host := range fwMap.Keys() {
		fwResults := fwMap[host]
		lineBreak := strings.Repeat("-", len(host))
		fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, host, lineBreak)

		iw := txtfmt.NewIndentWriter(out)
		if len(fwResults) == 0 {
			fmt.Fprintln(iw, "No SCM devices detected")
			continue
		}

		for _, res := range fwResults {
			err := printScmModule(&res.Module, iw)
			if err != nil {
				return err
			}

			iw1 := txtfmt.NewIndentWriter(iw)

			if res.Error != nil {
				fmt.Fprintf(iw1, "Error updating firmware: %s\n", res.Error.Error())
				continue
			}

			fmt.Fprint(iw1, "Success - The new firmware was staged. A reboot is required to apply.\n")
		}
	}

	return w.Err
}
