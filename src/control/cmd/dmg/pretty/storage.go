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
	"github.com/daos-stack/daos/src/control/server/storage"
)

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
			if err := control.PrintNvmeControllerSummary(controller, out, opts...); err != nil {
				return err
			}
			iw := txtfmt.NewIndentWriter(out)
			if err := control.PrintNvmeControllerHealth(controller.HealthStats, iw, opts...); err != nil {
				return err
			}
			fmt.Fprintln(out)
		}
	}

	return w.Err
}

// PrintNvmeMetaMap generates a human-readable representation of the supplied
// HostStorageMap, with a focus on presenting the NVMe Device Server Meta Data.
func PrintNvmeMetaMap(hsm control.HostStorageMap, out io.Writer, opts ...control.PrintConfigOption) error {
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
			if err := control.PrintNvmeControllerSummary(controller, out, opts...); err != nil {
				return err
			}
			iw := txtfmt.NewIndentWriter(out)
			if len(controller.SmdDevices) > 0 {
				fmt.Fprintln(iw, "SMD Devices")

				for _, device := range controller.SmdDevices {
					iw1 := txtfmt.NewIndentWriter(iw)
					if err := printSmdDevice(device, iw1, opts...); err != nil {
						return err
					}
				}
			} else {
				fmt.Fprintln(iw, "No devices found")
			}
			fmt.Fprintln(out)
		}
	}

	return w.Err
}

func printSmdDevice(dev *storage.SmdDevice, out io.Writer, opts ...control.PrintConfigOption) error {
	_, err := fmt.Fprintf(out, "UUID:%s Targets:%+v Rank:%d State:%s\n",
		dev.UUID, dev.TargetIDs, dev.Rank, dev.State)
	return err
}

func printSmdPool(pool *control.SmdPool, out io.Writer, opts ...control.PrintConfigOption) error {
	_, err := fmt.Fprintf(out, "Rank:%d Targets:%+v", pool.Rank, pool.TargetIDs)
	cfg := control.GetPrintConfig(opts...)
	if cfg.Verbose {
		_, err = fmt.Fprintf(out, " Blobs:%+v", pool.Blobs)
	}
	_, err = fmt.Fprintln(out)
	return err
}

// PrintSmdInfoMap generates a human-readable representation of the supplied
// HostStorageMap, with a focus on presenting the per-server metadata (SMD) information.
func PrintSmdInfoMap(req *control.SmdQueryReq, hsm control.HostStorageMap, out io.Writer, opts ...control.PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := control.GetPrintHosts(hss.HostSet.RangedString(), opts...)
		lineBreak := strings.Repeat("-", len(hosts))
		fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)

		iw := txtfmt.NewIndentWriter(out)
		if hss.HostStorage.SmdInfo == nil {
			fmt.Fprintln(iw, "No SMD info returned")
			continue
		}

		if !req.OmitDevices {
			if len(hss.HostStorage.SmdInfo.Devices) > 0 {
				fmt.Fprintln(iw, "Devices")

				for _, device := range hss.HostStorage.SmdInfo.Devices {
					iw1 := txtfmt.NewIndentWriter(iw)
					if err := printSmdDevice(device, iw1, opts...); err != nil {
						return err
					}
					if device.Health != nil {
						iw2 := txtfmt.NewIndentWriter(iw1)
						if err := control.PrintNvmeControllerHealth(device.Health, iw2, opts...); err != nil {
							return err
						}
						fmt.Fprintln(out)
					}
				}
			} else {
				fmt.Fprintln(iw, "No devices found")
			}
		}

		if !req.OmitPools {
			if len(hss.HostStorage.SmdInfo.Pools) > 0 {
				fmt.Fprintln(iw, "Pools")

				for uuid, poolSet := range hss.HostStorage.SmdInfo.Pools {
					iw1 := txtfmt.NewIndentWriter(iw)
					fmt.Fprintf(iw1, "UUID:%s\n", uuid)
					iw2 := txtfmt.NewIndentWriter(iw1)
					for _, pool := range poolSet {
						if err := printSmdPool(pool, iw2, opts...); err != nil {
							return err
						}
					}
					fmt.Fprintln(out)
				}
			} else {
				fmt.Fprintln(iw, "No pools found")
			}
		}
	}

	return w.Err
}

func printScmModule(module *storage.ScmModule, out io.Writer, opts ...control.PrintConfigOption) error {
	_, err := fmt.Fprintf(out, "%s\n", module.String())
	return err
}
