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
	"time"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func printNvmeController(nvme *storage.NvmeController, out io.Writer, opts ...control.PrintConfigOption) error {
	_, err := fmt.Fprintf(out, "PCI:%s Model:%s FW:%s Socket:%d Capacity:%s\n",
		nvme.PciAddr, nvme.Model, nvme.FwRev, nvme.SocketID, humanize.Bytes(nvme.Capacity()))
	return err
}

func printNvmeHealth(nvme *storage.NvmeController, out io.Writer, opts ...control.PrintConfigOption) error {
	stat := nvme.HealthStats

	if stat == nil {
		fmt.Fprintln(out, "Health Stats Unavailable")
		return nil
	}

	fmt.Fprintln(out, "Health Stats:")

	iw := txtfmt.NewIndentWriter(out)
	fmt.Fprintf(iw, "Temperature:%dK(%dC)\n", stat.Temp, stat.Temp-273)

	if stat.TempWarnTime > 0 {
		fmt.Fprintf(iw, "Temperature Warning Duration:%s\n",
			time.Duration(stat.TempWarnTime)*time.Minute)
	}
	if stat.TempCritTime > 0 {
		fmt.Fprintf(iw, "Temperature Critical Duration:%s\n",
			time.Duration(stat.TempCritTime)*time.Minute)
	}

	fmt.Fprintf(iw, "Controller Busy Time:%s\n", time.Duration(stat.CtrlBusyTime)*time.Minute)
	fmt.Fprintf(iw, "Power Cycles:%d\n", uint64(stat.PowerCycles))
	fmt.Fprintf(iw, "Power On Duration:%s\n", time.Duration(stat.PowerOnHours)*time.Hour)
	fmt.Fprintf(iw, "Unsafe Shutdowns:%d\n", uint64(stat.UnsafeShutdowns))
	fmt.Fprintf(iw, "Media Errors:%d\n", uint64(stat.MediaErrors))
	fmt.Fprintf(iw, "Error Log Entries:%d\n", uint64(stat.ErrorLogEntries))

	fmt.Fprintf(out, "Critical Warnings:\n")
	fmt.Fprintf(iw, "Temperature: ")
	if stat.TempWarn {
		fmt.Fprintf(iw, "WARNING\n")
	} else {
		fmt.Fprintf(iw, "OK\n")
	}
	fmt.Fprintf(iw, "Available Spare: ")
	if stat.AvailSpareWarn {
		fmt.Fprintf(iw, "WARNING\n")
	} else {
		fmt.Fprintf(iw, "OK\n")
	}
	fmt.Fprintf(iw, "Device Reliability: ")
	if stat.ReliabilityWarn {
		fmt.Fprintf(iw, "WARNING\n")
	} else {
		fmt.Fprintf(iw, "OK\n")
	}
	fmt.Fprintf(iw, "Read Only: ")
	if stat.ReadOnlyWarn {
		fmt.Fprintf(iw, "WARNING\n")
	} else {
		fmt.Fprintf(iw, "OK\n")
	}
	fmt.Fprintf(iw, "Volatile Memory Backup: ")
	if stat.VolatileWarn {
		fmt.Fprintf(iw, "WARNING\n")
	} else {
		fmt.Fprintf(iw, "OK\n")
	}

	return nil
}

// PrintNvmeHealthMap generates a human-readable representation of the supplied
// HostStorageMap, with a focus on presenting the NVMe Device Health information.
func PrintNvmeHealthMap(hsm control.HostStorageMap, out io.Writer, opts ...control.PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := control.GetPrintHosts(hss.HostSet.RangedString(), opts...)
		lineBreak := strings.Repeat("-", len(hosts))
		fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)

		if len(hss.HostStorage.NvmeDevices) == 0 {
			fmt.Fprintln(out, "  No NVMe devices detected")
			continue
		}

		for _, controller := range hss.HostStorage.NvmeDevices {
			if err := printNvmeController(controller, out, opts...); err != nil {
				return err
			}
			iw := txtfmt.NewIndentWriter(out)
			if err := printNvmeHealth(controller, iw, opts...); err != nil {
				return err
			}
			fmt.Fprintln(out)
		}
	}

	return w.Err
}
