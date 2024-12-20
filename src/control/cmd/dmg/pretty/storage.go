//
// (C) Copyright 2020-2024 Intel Corporation.
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

// printHostStorageMapVerbose generates a human-readable representation of the supplied
// HostStorageMap struct and writes it to the supplied io.Writer.
func printHostStorageMapVerbose(hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString(), opts...)
		lineBreak := strings.Repeat("-", len(hosts))
		fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)
		fmt.Fprintf(out, "HugePage Size: %d KB\n\n",
			hss.HostStorage.MemInfo.HugepageSizeKiB)
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
