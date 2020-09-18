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

package control

import (
	"fmt"
	"io"
	"sort"
	"strings"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// Provide pretty-printers for complex response types. The API should return structured
// responses so that callers can fully utilize them, but should also provide default
// human-readable representations so that callers aren't forced to implement printers.

var (
	defaultPrintConfig = &PrintConfig{
		Verbose:       false,
		ShowHostPorts: false,
	}
)

type (
	// PrintConfig defines parameters for controlling formatter behavior.
	PrintConfig struct {
		// Verbose indicates that the output should include more detail.
		Verbose bool
		// ShowHostPorts indicates that the host output should include the network port.
		ShowHostPorts bool
	}

	// PrintConfigOption defines a config function.
	PrintConfigOption func(*PrintConfig)
)

// PrintWithVerboseOutput toggles verbose output from the formatter.
func PrintWithVerboseOutput(verbose bool) PrintConfigOption {
	return func(cfg *PrintConfig) {
		cfg.Verbose = verbose
	}
}

// PrintWithHostPorts enables display of host ports in output.
func PrintWithHostPorts() PrintConfigOption {
	return func(cfg *PrintConfig) {
		cfg.ShowHostPorts = true
	}
}

// GetPrintConfig is a helper that returns a format configuration
// for a format function.
func GetPrintConfig(opts ...PrintConfigOption) *PrintConfig {
	cfg := &PrintConfig{}
	*cfg = *defaultPrintConfig
	for _, opt := range opts {
		opt(cfg)
	}
	return cfg
}

// GetPrintHosts is a helper that transforms the given list of
// host strings according to the format configuration.
func GetPrintHosts(in string, opts ...PrintConfigOption) string {
	var out []string
	fc := GetPrintConfig(opts...)

	for _, hostStr := range strings.Split(in, ",") {
		if fc.ShowHostPorts {
			out = append(out, hostStr)
			continue
		}

		hostPort := strings.Split(hostStr, ":")
		if len(hostPort) != 2 {
			out = append(out, hostStr)
			continue
		}
		out = append(out, hostPort[0])
	}

	return strings.Join(out, ",")
}

// PrintHostErrorsMap generates a human-readable representation of the supplied
// HostErrorsMap struct and writes it to the supplied io.Writer.
func PrintHostErrorsMap(hem HostErrorsMap, out io.Writer, opts ...PrintConfigOption) error {
	if len(hem) == 0 {
		return nil
	}

	setTitle := "Hosts"
	errTitle := "Error"

	tablePrint := txtfmt.NewTableFormatter(setTitle, errTitle)
	tablePrint.InitWriter(out)
	table := []txtfmt.TableRow{}

	for _, errStr := range hem.Keys() {
		errHosts := GetPrintHosts(hem[errStr].HostSet.RangedString(), opts...)
		row := txtfmt.TableRow{setTitle: errHosts}

		// Unpack the root cause error. If it's a fault,
		// just print the description.
		hostErr := errors.Cause(hem[errStr].HostError)
		row[errTitle] = hostErr.Error()
		if f, ok := hostErr.(*fault.Fault); ok {
			row[errTitle] = f.Description
		}

		table = append(table, row)
	}

	tablePrint.Format(table)
	return nil
}

func PrintResponseErrors(resp hostErrorsGetter, out io.Writer, opts ...PrintConfigOption) error {
	if resp == nil {
		return errors.Errorf("nil %T", resp)
	}

	if len(resp.getHostErrors()) > 0 {
		fmt.Fprintln(out, "Errors:")
		if err := PrintHostErrorsMap(resp.getHostErrors(), txtfmt.NewIndentWriter(out), opts...); err != nil {
			return err
		}
		fmt.Fprintln(out)
	}

	return nil
}

func PrintNvmeControllers(controllers storage.NvmeControllers, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	if len(controllers) == 0 {
		fmt.Fprintln(out, "\tNo NVMe devices found")
		return w.Err
	}

	pciTitle := "NVMe PCI"
	modelTitle := "Model"
	fwTitle := "FW Revision"
	socketTitle := "Socket ID"
	capacityTitle := "Capacity"

	formatter := txtfmt.NewTableFormatter(
		pciTitle, modelTitle, fwTitle, socketTitle, capacityTitle,
	)
	formatter.InitWriter(out)
	var table []txtfmt.TableRow

	sort.Slice(controllers, func(i, j int) bool { return controllers[i].PciAddr < controllers[j].PciAddr })

	for _, ctrlr := range controllers {
		row := txtfmt.TableRow{pciTitle: ctrlr.PciAddr}
		row[modelTitle] = ctrlr.Model
		row[fwTitle] = ctrlr.FwRev
		row[socketTitle] = fmt.Sprint(ctrlr.SocketID)
		row[capacityTitle] = humanize.Bytes(ctrlr.Capacity())

		table = append(table, row)
	}

	formatter.Format(table)
	return w.Err
}

func PrintNvmeControllerSummary(nvme *storage.NvmeController, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	if _, err := fmt.Fprintf(out, "PCI:%s Model:%s FW:%s Socket:%d Capacity:%s\n",
		nvme.PciAddr, nvme.Model, nvme.FwRev, nvme.SocketID, humanize.Bytes(nvme.Capacity())); err != nil {
		return err
	}

	return w.Err
}

func PrintNvmeControllerHealth(stat *storage.NvmeControllerHealth, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	if stat == nil {
		fmt.Fprintln(out, "Health Stats Unavailable")
		return w.Err
	}

	fmt.Fprintln(out, "Health Stats:")

	iw := txtfmt.NewIndentWriter(out)

	if stat.Timestamp > 0 {
		fmt.Fprintf(iw, "Timestamp:%s\n", time.Time(time.Unix(int64(stat.Timestamp), 0)))
	}

	fmt.Fprintf(iw, "Temperature:%dK(%.02fC)\n", stat.TempK(), stat.TempC())

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
	fmt.Fprintf(iw, "Error Count:%d\n", uint64(stat.ErrorCount))
	fmt.Fprintf(iw, "Media Errors:%d\n", uint64(stat.MediaErrors))
	fmt.Fprintf(iw, "Read Errors:%d\n", uint64(stat.ReadErrors))
	fmt.Fprintf(iw, "Write Errors:%d\n", uint64(stat.WriteErrors))
	fmt.Fprintf(iw, "Unmap Errors:%d\n", uint64(stat.UnmapErrors))
	fmt.Fprintf(iw, "Checksum Errors:%d\n", uint64(stat.ChecksumErrors))
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

	return w.Err
}

func PrintScmModules(modules storage.ScmModules, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	if len(modules) == 0 {
		fmt.Fprintln(out, "\tNo SCM modules found")
		return w.Err
	}

	physicalIdTitle := "SCM Module ID"
	socketTitle := "Socket ID"
	memCtrlrTitle := "Memory Ctrlr ID"
	channelTitle := "Channel ID"
	slotTitle := "Channel Slot"
	capacityTitle := "Capacity"

	formatter := txtfmt.NewTableFormatter(
		physicalIdTitle, socketTitle, memCtrlrTitle, channelTitle, slotTitle, capacityTitle,
	)
	formatter.InitWriter(out)
	var table []txtfmt.TableRow

	sort.Slice(modules, func(i, j int) bool { return modules[i].PhysicalID < modules[j].PhysicalID })

	for _, m := range modules {
		row := txtfmt.TableRow{physicalIdTitle: fmt.Sprint(m.PhysicalID)}
		row[socketTitle] = fmt.Sprint(m.SocketID)
		row[memCtrlrTitle] = fmt.Sprint(m.ControllerID)
		row[channelTitle] = fmt.Sprint(m.ChannelID)
		row[slotTitle] = fmt.Sprint(m.ChannelPosition)
		row[capacityTitle] = humanize.IBytes(m.Capacity)

		table = append(table, row)
	}

	formatter.Format(table)
	return w.Err
}

func PrintScmNamespaces(namespaces storage.ScmNamespaces, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	if len(namespaces) == 0 {
		fmt.Fprintln(out, "\tNo SCM namespaces found")
		return w.Err
	}

	deviceTitle := "SCM Namespace"
	socketTitle := "Socket ID"
	capacityTitle := "Capacity"

	formatter := txtfmt.NewTableFormatter(deviceTitle, socketTitle, capacityTitle)
	formatter.InitWriter(out)
	var table []txtfmt.TableRow

	sort.Slice(namespaces, func(i, j int) bool { return namespaces[i].BlockDevice < namespaces[j].BlockDevice })

	for _, ns := range namespaces {
		row := txtfmt.TableRow{deviceTitle: ns.BlockDevice}
		row[socketTitle] = fmt.Sprint(ns.NumaNode)
		row[capacityTitle] = humanize.Bytes(ns.Size)

		table = append(table, row)
	}

	formatter.Format(table)
	return w.Err
}

func printScmMountPoints(mountpoints storage.ScmMountPoints, out io.Writer, opts ...PrintConfigOption) error {
	if len(mountpoints) == 0 {
		fmt.Fprintln(out, "\tNo SCM mount results")
		return nil
	}

	mntTitle := "SCM Mount"
	resultTitle := "Format Result"

	formatter := txtfmt.NewTableFormatter(mntTitle, resultTitle)
	formatter.InitWriter(out)
	var table []txtfmt.TableRow

	sort.Slice(mountpoints, func(i, j int) bool { return mountpoints[i].Path < mountpoints[j].Path })

	for _, mountpoint := range mountpoints {
		row := txtfmt.TableRow{mntTitle: mountpoint.Path}
		row[resultTitle] = mountpoint.Info

		table = append(table, row)
	}

	formatter.Format(table)
	return nil
}

// printHostStorageMapVerbose generates a human-readable representation of the supplied
// HostStorageMap struct and writes it to the supplied io.Writer.
func printHostStorageMapVerbose(hsm HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := GetPrintHosts(hss.HostSet.RangedString(), opts...)
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
func PrintHostStorageMap(hsm HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
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
		hosts := GetPrintHosts(hss.HostSet.RangedString(), opts...)
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
func PrintStoragePrepareMap(hsm HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
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
		hosts := GetPrintHosts(hss.HostSet.RangedString(), opts...)
		row := txtfmt.TableRow{hostsTitle: hosts}
		row[scmTitle] = hss.HostStorage.ScmNamespaces.Summary()
		row[rebootTitle] = fmt.Sprintf("%t", hss.HostStorage.RebootRequired)
		table = append(table, row)
	}

	tablePrint.Format(table)
	return nil
}

func printNvmeFormatResults(devices storage.NvmeControllers, out io.Writer, opts ...PrintConfigOption) error {
	if len(devices) == 0 {
		fmt.Fprintln(out, "\tNo NVMe devices found")
		return nil
	}

	pciTitle := "NVMe PCI"
	resultTitle := "Format Result"

	formatter := txtfmt.NewTableFormatter(pciTitle, resultTitle)
	formatter.InitWriter(out)
	var table []txtfmt.TableRow

	sort.Slice(devices, func(i, j int) bool { return devices[i].PciAddr < devices[j].PciAddr })

	for _, device := range devices {
		row := txtfmt.TableRow{pciTitle: device.PciAddr}
		row[resultTitle] = device.Info

		table = append(table, row)
	}

	formatter.Format(table)
	return nil
}

func printStorageFormatMapVerbose(hsm HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := GetPrintHosts(hss.HostSet.RangedString(), opts...)
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
func PrintStorageFormatMap(hsm HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
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
		hosts := GetPrintHosts(hss.HostSet.RangedString(), opts...)
		row := txtfmt.TableRow{hostsTitle: hosts}
		row[scmTitle] = fmt.Sprintf("%d", len(hss.HostStorage.ScmMountPoints))
		row[nvmeTitle] = fmt.Sprintf("%d", len(hss.HostStorage.NvmeDevices))
		table = append(table, row)
	}

	tablePrint.Format(table)
	return nil
}
