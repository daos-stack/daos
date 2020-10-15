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

// printHostStorageMapVerbose generates a human-readable representation of the supplied
// HostStorageMap struct and writes it to the supplied io.Writer.
func printHostStorageMapVerbose(hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString(), opts...)
		lineBreak := strings.Repeat("-", len(hosts))
		fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)
		if len(hss.HostStorage.ScmNamespaces) == 0 {
			if err := PrintScmModules(hss.HostStorage.ScmModules, out, opts...); err != nil {
				return err
			}
		} else {
			if err := PrintScmNamespaces(hss.HostStorage.ScmNamespaces, out, opts...); err != nil {
				return err
			}
		}
		fmt.Fprintln(out)
		if err := PrintNvmeControllers(hss.HostStorage.NvmeDevices, out, opts...); err != nil {
			return err
		}
		fmt.Fprintln(out)
	}

	return nil
}

// PrintHostStorageMap generates a human-readable representation of the supplied
// HostStorageMap struct and writes it to the supplied io.Writer.
func PrintHostStorageMap(hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	if len(hsm) == 0 {
		return nil
	}
	fc := GetPrintConfig(opts...)

	if fc.Verbose {
		return printHostStorageMapVerbose(hsm, out, opts...)
	}

	hostsTitle := "Hosts"
	scmTitle := "SCM Total"
	nvmeTitle := "NVMe Total"

	tablePrint := txtfmt.NewTableFormatter(hostsTitle, scmTitle, nvmeTitle)
	tablePrint.InitWriter(out)
	table := []txtfmt.TableRow{}

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString(), opts...)
		row := txtfmt.TableRow{hostsTitle: hosts}
		if len(hss.HostStorage.ScmNamespaces) == 0 {
			row[scmTitle] = hss.HostStorage.ScmModules.Summary()
		} else {
			row[scmTitle] = hss.HostStorage.ScmNamespaces.Summary()
		}
		row[nvmeTitle] = hss.HostStorage.NvmeDevices.Summary()
		table = append(table, row)
	}

	tablePrint.Format(table)
	return nil
}

// PrintStoragePrepareMap generates a human-readable representation of the supplied
// HostStorageMap which is populated in response to a StoragePrepare operation.
func PrintStoragePrepareMap(hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	if len(hsm) == 0 {
		return nil
	}

	hostsTitle := "Hosts"
	scmTitle := "SCM Namespaces"
	rebootTitle := "Reboot Required"

	fmt.Fprintln(out, "Prepare Results:")
	tablePrint := txtfmt.NewTableFormatter(hostsTitle, scmTitle, rebootTitle)
	tablePrint.InitWriter(txtfmt.NewIndentWriter(out))
	table := []txtfmt.TableRow{}

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString(), opts...)
		row := txtfmt.TableRow{hostsTitle: hosts}
		row[scmTitle] = hss.HostStorage.ScmNamespaces.Summary()
		row[rebootTitle] = fmt.Sprintf("%t", hss.HostStorage.RebootRequired)
		table = append(table, row)
	}

	tablePrint.Format(table)
	return nil
}

func printStorageFormatMapVerbose(hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString(), opts...)
		lineBreak := strings.Repeat("-", len(hosts))
		fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)
		if err := printScmMountPoints(hss.HostStorage.ScmMountPoints, out, opts...); err != nil {
			return err
		}
		fmt.Fprintln(out)
		if err := printNvmeFormatResults(hss.HostStorage.NvmeDevices, out, opts...); err != nil {
			return err
		}
		fmt.Fprintln(out)
	}

	return nil
}

// PrintStorageFormatMap generates a human-readable representation of the supplied
// HostStorageMap which is populated in response to a StorageFormat operation.
func PrintStorageFormatMap(hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	if len(hsm) == 0 {
		return nil
	}
	fc := GetPrintConfig(opts...)

	if fc.Verbose {
		return printStorageFormatMapVerbose(hsm, out, opts...)
	}

	hostsTitle := "Hosts"
	scmTitle := "SCM Devices"
	nvmeTitle := "NVMe Devices"

	fmt.Fprintln(out, "Format Summary:")
	tablePrint := txtfmt.NewTableFormatter(hostsTitle, scmTitle, nvmeTitle)
	tablePrint.InitWriter(txtfmt.NewIndentWriter(out))
	table := []txtfmt.TableRow{}

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString(), opts...)
		row := txtfmt.TableRow{hostsTitle: hosts}
		row[scmTitle] = fmt.Sprintf("%d", len(hss.HostStorage.ScmMountPoints))
		row[nvmeTitle] = fmt.Sprintf("%d", len(hss.HostStorage.NvmeDevices))
		table = append(table, row)
	}

	tablePrint.Format(table)
	return nil
}

func printSmdDevice(dev *storage.SmdDevice, out io.Writer, opts ...PrintConfigOption) error {
	_, err := fmt.Fprintf(out, "UUID:%s Targets:%+v Rank:%d State:%s\n",
		dev.UUID, dev.TargetIDs, dev.Rank, dev.State)
	return err
}

func printSmdPool(pool *control.SmdPool, out io.Writer, opts ...PrintConfigOption) error {
	_, err := fmt.Fprintf(out, "Rank:%d Targets:%+v", pool.Rank, pool.TargetIDs)
	cfg := GetPrintConfig(opts...)
	if cfg.Verbose {
		_, err = fmt.Fprintf(out, " Blobs:%+v", pool.Blobs)
	}
	_, err = fmt.Fprintln(out)
	return err
}

// PrintSmdInfoMap generates a human-readable representation of the supplied
// HostStorageMap, with a focus on presenting the per-server metadata (SMD) information.
func PrintSmdInfoMap(req *control.SmdQueryReq, hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString(), opts...)
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
						if err := printNvmeHealth(device.Health, iw2, opts...); err != nil {
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
