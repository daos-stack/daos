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
	"sort"
	"strings"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	scmUpdateSuccess = "Success - The new firmware was staged. A reboot is required to apply."
	scmNotFound      = "No SCM devices detected"
	errorPrefix      = "Error"
)

// hostDeviceSet represents a collection of hosts and devices on those hosts.
type hostDeviceSet struct {
	Hosts     *hostlist.HostSet
	Devices   []string
	IsSuccess bool
}

// AddHost adds a host to the set.
func (h *hostDeviceSet) AddHost(host string) {
	h.Hosts.Insert(host)
}

// AddDevice adds a device to the set.
func (h *hostDeviceSet) AddDevice(device string) {
	h.Devices = append(h.Devices, device)
}

// newHostDeviceSet creates and initializes an empty hostDeviceSet.
func newHostDeviceSet() (*hostDeviceSet, error) {
	hosts, err := hostlist.CreateSet("")
	if err != nil {
		return nil, err
	}
	return &hostDeviceSet{
		Hosts: hosts,
	}, nil
}

// hostDeviceResultMap is a map from a result string to a hostDeviceSet.
type hostDeviceResultMap map[string]*hostDeviceSet

// AddHostDevice adds a host and a device to the map set for a given result string.
func (m hostDeviceResultMap) AddHostDevice(resultStr string, host string, device string) error {
	err := m.AddHost(resultStr, host)
	if err != nil {
		return err
	}
	m[resultStr].AddDevice(device)
	return nil
}

// AddHost adds a host to the map set for a given result string.
func (m hostDeviceResultMap) AddHost(resultStr string, host string) error {
	if _, ok := m[resultStr]; !ok {
		newSet, err := newHostDeviceSet()
		if err != nil {
			return err
		}
		m[resultStr] = newSet
	}

	m[resultStr].AddHost(host)
	return nil
}

// SetSuccess notes that the result is a success
func (m hostDeviceResultMap) SetSuccess(resultStr string) {
	m[resultStr].IsSuccess = true
}

// Keys returns the sorted keys of the map.
func (m hostDeviceResultMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

// PrintSCMFirmwareQueryMap formats the firmware query results in a condensed format.
func PrintSCMFirmwareQueryMap(fwMap control.HostSCMQueryMap, out io.Writer,
	opts ...control.PrintConfigOption) error {
	condensed, err := condenseSCMQueryMap(fwMap)
	if err != nil {
		return err
	}

	return printCondensedMap(condensed, out, opts...)
}

func condenseSCMQueryMap(fwMap control.HostSCMQueryMap) (hostDeviceResultMap, error) {
	condensed := make(hostDeviceResultMap)
	for _, host := range fwMap.Keys() {
		results := fwMap[host]
		if len(results) == 0 {
			err := condensed.AddHost(scmNotFound, host)
			if err != nil {
				return nil, err
			}
			continue
		}

		for _, devRes := range results {
			var resultStr string
			if devRes.Error == nil {
				var b strings.Builder
				printSCMFirmwareInfo(devRes.Info, &b)
				resultStr = b.String()
			} else {
				resultStr = getPrintErrorString(devRes.Error.Error())
			}

			err := condensed.AddHostDevice(resultStr, host, devRes.Module.String())
			if err != nil {
				return nil, err
			}

			if devRes.Error == nil {
				condensed.SetSuccess(resultStr)
			}
		}
	}
	return condensed, nil
}

func printSCMFirmwareInfo(info *storage.ScmFirmwareInfo, out io.Writer) {
	if info == nil {
		fmt.Fprint(out, getPrintErrorString("No information available"))
		return
	}

	fmt.Fprintf(out, "Active Version: %s\n", getPrintVersion(info.ActiveVersion))
	fmt.Fprintf(out, "Staged Version: %s\n", getPrintVersion(info.StagedVersion))
	fmt.Fprintf(out, "Maximum Firmware Image Size: %s\n", humanize.IBytes(uint64(info.ImageMaxSizeBytes)))
	fmt.Fprintf(out, "Last Update Status: %s", info.UpdateStatus)
}

func getPrintErrorString(errorStr string) string {
	return fmt.Sprintf("%s: %s", errorPrefix, errorStr)
}

func getPrintVersion(version string) string {
	if version == "" {
		return "N/A"
	}
	return version
}

func printCondensedMap(condensed hostDeviceResultMap, out io.Writer, opts ...control.PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)
	for _, result := range condensed.Keys() {
		set, ok := condensed[result]
		if !ok {
			continue
		}
		hosts := control.GetPrintHosts(set.Hosts.RangedString(), opts...)
		printHostHeader(hosts, out)

		iw := txtfmt.NewIndentWriter(out)
		fmt.Fprintln(iw, result)

		if len(set.Devices) == 0 {
			continue
		}

		iw2 := txtfmt.NewIndentWriter(iw)
		if set.IsSuccess {
			fmt.Fprintf(iw2, "%d devices\n", len(set.Devices))
		} else {
			// The user probably cares which devices failed, to we'll
			// be a little noisy here.
			for _, dev := range set.Devices {
				fmt.Fprintln(iw2, dev)
			}
		}

	}

	return w.Err
}

func printHostHeader(hosts string, out io.Writer) {
	lineBreak := strings.Repeat("-", len(hosts))
	fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)
}

// PrintSCMFirmwareQueryMapVerbose formats the firmware query results in a detailed format.
func PrintSCMFirmwareQueryMapVerbose(fwMap control.HostSCMQueryMap, out io.Writer,
	opts ...control.PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	for _, host := range fwMap.Keys() {
		printHostHeader(host, out)

		iw := txtfmt.NewIndentWriter(out)
		fwResults := fwMap[host]
		if len(fwResults) == 0 {
			fmt.Fprintln(iw, scmNotFound)
			continue
		}

		for _, res := range fwResults {
			err := printScmModule(&res.Module, iw)
			if err != nil {
				return err
			}

			iw2 := txtfmt.NewIndentWriter(iw)

			if res.Error != nil {
				fmt.Fprintln(iw2, getPrintErrorString(res.Error.Error()))
				continue
			}

			printSCMFirmwareInfo(res.Info, iw2)
			fmt.Fprintf(iw2, "\n")
		}
	}

	return w.Err
}

// PrintSCMFirmwareUpdateMap prints the update results in a condensed format.
func PrintSCMFirmwareUpdateMap(fwMap control.HostSCMUpdateMap, out io.Writer,
	opts ...control.PrintConfigOption) error {
	condensed, err := condenseSCMUpdateMap(fwMap)
	if err != nil {
		return err
	}

	return printCondensedMap(condensed, out, opts...)
}

func condenseSCMUpdateMap(fwMap control.HostSCMUpdateMap) (hostDeviceResultMap, error) {
	condensed := make(hostDeviceResultMap)
	for _, host := range fwMap.Keys() {
		results := fwMap[host]
		if len(results) == 0 {
			err := condensed.AddHost(scmNotFound, host)
			if err != nil {
				return nil, err
			}
			continue
		}

		for _, devRes := range results {
			resultStr := scmUpdateSuccess
			if devRes.Error != nil {
				resultStr = getPrintErrorString(devRes.Error.Error())
			}

			err := condensed.AddHostDevice(resultStr, host, devRes.Module.String())
			if err != nil {
				return nil, err
			}
			if devRes.Error == nil {
				condensed.SetSuccess(resultStr)
			}
		}
	}
	return condensed, nil
}

// PrintSCMFirmwareUpdateMapVerbose formats the firmware update results in a
// detailed format.
func PrintSCMFirmwareUpdateMapVerbose(fwMap control.HostSCMUpdateMap, out io.Writer,
	opts ...control.PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	for _, host := range fwMap.Keys() {
		printHostHeader(host, out)

		iw := txtfmt.NewIndentWriter(out)
		fwResults := fwMap[host]
		if len(fwResults) == 0 {
			fmt.Fprintln(iw, scmNotFound)
			continue
		}

		for _, res := range fwResults {
			err := printScmModule(&res.Module, iw)
			if err != nil {
				return err
			}

			iw2 := txtfmt.NewIndentWriter(iw)

			if res.Error != nil {
				fmt.Fprintf(iw2, "%s: %s\n", errorPrefix, res.Error.Error())
				continue
			}

			fmt.Fprintf(iw2, "%s\n", scmUpdateSuccess)
		}
	}

	return w.Err
}
