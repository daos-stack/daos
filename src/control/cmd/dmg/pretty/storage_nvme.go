//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"
	"sort"
	"strings"
	"time"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func printNvmeControllerSummary(nvme *storage.NvmeController, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	if _, err := fmt.Fprintf(out, "PCI:%s Model:%s FW:%s Socket:%d Capacity:%s\n",
		nvme.PciAddr, nvme.Model, nvme.FwRev, nvme.SocketID, humanize.Bytes(nvme.Capacity())); err != nil {
		return err
	}

	return w.Err
}

func getTimestampString(secs uint64) string {
	if secs == 0 {
		return "N/A"
	}
	return common.FormatTime(time.Unix(int64(secs), 0))
}

func printNvmeHealth(stat *storage.NvmeHealth, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	if stat == nil {
		fmt.Fprintln(out, "Health Stats Unavailable")
		return w.Err
	}

	fmt.Fprintln(out, "Health Stats:")

	iw := txtfmt.NewIndentWriter(out)

	if stat.Timestamp > 0 {
		fmt.Fprintf(iw, "Timestamp:%s\n", getTimestampString(stat.Timestamp))
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
	fmt.Fprintf(iw, "Media Errors:%d\n", uint64(stat.MediaErrors))
	if stat.Timestamp > 0 {
		fmt.Fprintf(iw, "Read Errors:%d\n", uint64(stat.ReadErrors))
		fmt.Fprintf(iw, "Write Errors:%d\n", uint64(stat.WriteErrors))
		fmt.Fprintf(iw, "Unmap Errors:%d\n", uint64(stat.UnmapErrors))
		fmt.Fprintf(iw, "Checksum Errors:%d\n", uint64(stat.ChecksumErrors))
	}
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

	if stat.ProgFailCntNorm > 0 {
		fmt.Fprintf(out, "Intel Vendor SMART Attributes:\n")
		fmt.Fprintf(iw, "Program Fail Count:\n")
		fmt.Fprintf(iw, "   Normalized:%d%%\n",
			uint8(stat.ProgFailCntNorm))
		fmt.Fprintf(iw, "   Raw:%d\n",
			uint64(stat.ProgFailCntRaw))
		fmt.Fprintf(iw, "Erase Fail Count:\n")
		fmt.Fprintf(iw, "   Normalized:%d%%\n",
			uint8(stat.EraseFailCntNorm))
		fmt.Fprintf(iw, "   Raw:%d\n",
			uint64(stat.EraseFailCntRaw))
		fmt.Fprintf(iw, "Wear Leveling Count:\n")
		fmt.Fprintf(iw, "   Normalized:%d%%\n",
			uint8(stat.WearLevelingCntNorm))
		fmt.Fprintf(iw, "   Min:%d\n",
			uint16(stat.WearLevelingCntMin))
		fmt.Fprintf(iw, "   Max:%d\n",
			uint16(stat.WearLevelingCntMax))
		fmt.Fprintf(iw, "   Avg:%d\n",
			uint16(stat.WearLevelingCntAvg))
		fmt.Fprintf(iw, "End-to-End Error Detection Count:%d\n",
			uint64(stat.EndtoendErrCntRaw))
		fmt.Fprintf(iw, "CRC Error Count:%d\n",
			uint64(stat.CrcErrCntRaw))
		fmt.Fprintf(iw, "Timed Workload, Media Wear:%d\n",
			uint64(stat.MediaWearRaw))
		fmt.Fprintf(iw, "Timed Workload, Host Read/Write Ratio:%d\n",
			uint64(stat.HostReadsRaw))
		fmt.Fprintf(iw, "Timed Workload, Timer:%d\n",
			uint64(stat.WorkloadTimerRaw))
		fmt.Fprintf(iw, "Thermal Throttle Status:%d%%\n",
			uint8(stat.ThermalThrottleStatus))
		fmt.Fprintf(iw, "Thermal Throttle Event Count:%d\n",
			uint64(stat.ThermalThrottleEventCnt))
		fmt.Fprintf(iw, "Retry Buffer Overflow Counter:%d\n",
			uint64(stat.RetryBufferOverflowCnt))
		fmt.Fprintf(iw, "PLL Lock Loss Count:%d\n",
			uint64(stat.PllLockLossCnt))
		fmt.Fprintf(iw, "NAND Bytes Written:%d\n",
			uint64(stat.NandBytesWritten))
		fmt.Fprintf(iw, "Host Bytes Written:%d\n",
			uint64(stat.HostBytesWritten))
	}

	return w.Err
}

// Strip NVMe controller format results of skip entries.
func parseNvmeFormatResults(inResults storage.NvmeControllers) storage.NvmeControllers {
	parsedResults := make(storage.NvmeControllers, 0, len(inResults))
	for _, result := range inResults {
		if result.PciAddr != storage.NilBdevAddress {
			// ignore skip results
			parsedResults = append(parsedResults, result)
		}
	}

	return parsedResults
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

	for _, device := range parseNvmeFormatResults(devices) {
		row := txtfmt.TableRow{pciTitle: device.PciAddr}
		row[resultTitle] = device.Info

		table = append(table, row)
	}

	formatter.Format(table)
	return nil
}

// PrintNvmeControllers displays controller details in a verbose table.
//
// TODO: un-export function when not needed in cmd/daos_server/storage.go
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

// PrintNvmeHealthMap generates a human-readable representation of the supplied
// HostStorageMap, with a focus on presenting the NVMe Device Health information.
func PrintNvmeHealthMap(hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString(), opts...)
		lineBreak := strings.Repeat("-", len(hosts))
		fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)

		if len(hss.HostStorage.NvmeDevices) == 0 {
			fmt.Fprintln(out, "  No NVMe devices detected")
			continue
		}

		for _, controller := range hss.HostStorage.NvmeDevices {
			if err := printNvmeControllerSummary(controller, out, opts...); err != nil {
				return err
			}
			iw := txtfmt.NewIndentWriter(out)
			if err := printNvmeHealth(controller.HealthStats, iw, opts...); err != nil {
				return err
			}
			fmt.Fprintln(out)
		}
	}

	return w.Err
}

// PrintNvmeMetaMap generates a human-readable representation of the supplied
// HostStorageMap, with a focus on presenting the NVMe Device Server Meta Data.
func PrintNvmeMetaMap(hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString(), opts...)
		lineBreak := strings.Repeat("-", len(hosts))
		fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)

		if len(hss.HostStorage.NvmeDevices) == 0 {
			fmt.Fprintln(out, "  No NVMe devices detected")
			continue
		}

		for _, controller := range hss.HostStorage.NvmeDevices {
			if err := printNvmeControllerSummary(controller, out, opts...); err != nil {
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
				fmt.Fprintln(iw, "No SMD devices found")
			}
			fmt.Fprintln(out)
		}
	}

	return w.Err
}
