//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"
	"strings"

	"github.com/dustin/go-humanize"

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
		fmt.Fprintf(out, "HugePage Size: %d KB\n", hss.HostStorage.MemInfo.HugepageSizeKiB)
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
	fc := getPrintConfig(opts...)

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

// PrintHostStorageUsageMap generates a human-readable representation of the supplied
// HostStorageMap struct and writes utilization info to the supplied io.Writer.
func PrintHostStorageUsageMap(hsm control.HostStorageMap, out io.Writer) error {
	if len(hsm) == 0 {
		return nil
	}

	hostsTitle := "Hosts"
	scmTitle := "SCM-Total"
	scmFreeTitle := "SCM-Free"
	scmUsageTitle := "SCM-Used"
	nvmeTitle := "NVMe-Total"
	nvmeFreeTitle := "NVMe-Free"
	nvmeUsageTitle := "NVMe-Used"

	tablePrint := txtfmt.NewTableFormatter(hostsTitle, scmTitle, scmFreeTitle,
		scmUsageTitle, nvmeTitle, nvmeFreeTitle, nvmeUsageTitle)
	tablePrint.InitWriter(out)
	table := []txtfmt.TableRow{}

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString())
		row := txtfmt.TableRow{hostsTitle: hosts}
		storage := hss.HostStorage
		row[scmTitle] = humanize.Bytes(storage.ScmNamespaces.Total())
		row[scmFreeTitle] = humanize.Bytes(storage.ScmNamespaces.Free())
		row[scmUsageTitle] = storage.ScmNamespaces.PercentUsage()
		row[nvmeTitle] = humanize.Bytes(storage.NvmeDevices.Total())
		row[nvmeFreeTitle] = humanize.Bytes(storage.NvmeDevices.Free())
		row[nvmeUsageTitle] = storage.NvmeDevices.PercentUsage()
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
	fc := getPrintConfig(opts...)

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
		row[nvmeTitle] = fmt.Sprintf("%d",
			len(parseNvmeFormatResults(hss.HostStorage.NvmeDevices)))
		table = append(table, row)
	}

	tablePrint.Format(table)
	return nil
}

func printSmdDevice(dev *storage.SmdDevice, iw io.Writer, opts ...PrintConfigOption) error {
	fc := getPrintConfig(opts...)

	if fc.LEDInfoOnly {
		if _, err := fmt.Fprintf(iw, "TrAddr:%s", dev.TrAddr); err != nil {
			return err
		}
		if dev.UUID != "" {
			if _, err := fmt.Fprintf(iw, " [UUID:%s]", dev.UUID); err != nil {
				return err
			}
		}
		if _, err := fmt.Fprintf(iw, " LED:%s\n", dev.LedState); err != nil {
			return err
		}
		return nil
	}

	if _, err := fmt.Fprintf(iw, "UUID:%s [TrAddr:%s]\n", dev.UUID, dev.TrAddr); err != nil {
		return err
	}

	var hasSysXS string
	if dev.HasSysXS {
		hasSysXS = "SysXS "
	}
	if _, err := fmt.Fprintf(txtfmt.NewIndentWriter(iw),
		"Roles:%s %sTargets:%+v Rank:%d State:%s LED:%s\n", dev.Roles.String(), hasSysXS,
		dev.TargetIDs, dev.Rank, dev.NvmeState.String(), dev.LedState); err != nil {
		return err
	}

	return nil
}

func printSmdPool(pool *control.SmdPool, out io.Writer, opts ...PrintConfigOption) error {
	ew := txtfmt.NewErrWriter(out)
	fmt.Fprintf(ew, "Rank:%d Targets:%+v", pool.Rank, pool.TargetIDs)
	cfg := getPrintConfig(opts...)
	if cfg.Verbose {
		fmt.Fprintf(ew, " Blobs:%+v", pool.Blobs)
	}
	fmt.Fprintln(ew)
	return ew.Err
}

// PrintSmdInfoMap generates a human-readable representation of the supplied
// HostStorageMap, with a focus on presenting the per-server metadata (SMD) information.
func PrintSmdInfoMap(omitDevs, omitPools bool, hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
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

		if !omitDevs {
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

		if !omitPools {
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
