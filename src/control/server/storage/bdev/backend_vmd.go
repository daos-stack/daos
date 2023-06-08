//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"bufio"
	"bytes"
	"fmt"
	"os/exec"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// mapVMDToBackingDevs stores found vmd backing device details under vmd address key.
func mapVMDToBackingDevs(foundCtrlrs storage.NvmeControllers) (map[string]storage.NvmeControllers, error) {
	vmds := make(map[string]storage.NvmeControllers)

	for _, ctrlr := range foundCtrlrs {
		addr, err := hardware.NewPCIAddress(ctrlr.PciAddr)
		if err != nil {
			return nil, errors.Wrap(err, "controller pci address invalid")
		}

		vmdAddr, err := addr.BackingToVMDAddress()
		if err != nil {
			if err == hardware.ErrNotVMDBackingAddress {
				continue
			}
			return nil, err
		}

		if _, exists := vmds[vmdAddr.String()]; !exists {
			vmds[vmdAddr.String()] = make(storage.NvmeControllers, 0)
		}

		// add backing device details to vmd address key in map
		vmds[vmdAddr.String()] = append(vmds[vmdAddr.String()], ctrlr)
	}

	return vmds, nil
}

// mapVMDToBackingAddrs stores found vmd backing device addresses under vmd address key.
func mapVMDToBackingAddrs(foundCtrlrs storage.NvmeControllers) (map[string]*hardware.PCIAddressSet, error) {
	vmds := make(map[string]*hardware.PCIAddressSet)

	ctrlrAddrs, err := foundCtrlrs.Addresses()
	if err != nil {
		return nil, err
	}

	for _, addr := range ctrlrAddrs.Addresses() {
		vmdAddr, err := addr.BackingToVMDAddress()
		if err != nil {
			if err == hardware.ErrNotVMDBackingAddress {
				continue
			}
			return nil, err
		}

		if _, exists := vmds[vmdAddr.String()]; !exists {
			vmds[vmdAddr.String()] = new(hardware.PCIAddressSet)
		}

		// add backing device address to vmd address key in map
		if err := vmds[vmdAddr.String()].Add(addr); err != nil {
			return nil, err
		}
	}

	return vmds, nil
}

// substVMDAddrs replaces VMD endpoint PCI addresses in input device list with the PCI
// addresses of the backing devices behind the VMD endpoint.
//
// Return new device list with PCI addresses of devices behind the VMD.
//
// Add addresses that are not VMD endpoints to the output list.
func substVMDAddrs(inPCIAddrs *hardware.PCIAddressSet, foundCtrlrs storage.NvmeControllers) (*hardware.PCIAddressSet, error) {
	if len(foundCtrlrs) == 0 {
		return inPCIAddrs, nil
	}

	vmds, err := mapVMDToBackingAddrs(foundCtrlrs)
	if err != nil {
		return nil, err
	}

	// Swap any input VMD endpoint addresses with respective backing device addresses.
	// inAddr entries that are already backing device addresses will be added to output list.
	outPCIAddrs := new(hardware.PCIAddressSet)
	for _, inAddr := range inPCIAddrs.Addresses() {
		toAdd := []*hardware.PCIAddress{inAddr}

		if backing, exists := vmds[inAddr.String()]; exists {
			toAdd = backing.Addresses()
		}

		if err := outPCIAddrs.Add(toAdd...); err != nil {
			return nil, err
		}
	}

	return outPCIAddrs, nil
}

// substituteVMDAddresses wraps around substVMDAddrs to substitute VMD addresses with the relevant
// backing device addresses.
// Function takes a BdevScanResponse reference to derive address map and a logger.
func substituteVMDAddresses(log logging.Logger, inPCIAddrs *hardware.PCIAddressSet, bdevCache *storage.BdevScanResponse) (*hardware.PCIAddressSet, error) {
	if inPCIAddrs == nil {
		return nil, errors.New("nil input PCIAddressSet")
	}
	if bdevCache == nil || len(bdevCache.Controllers) == 0 {
		log.Debugf("no bdev cache to find vmd backing devices (devs: %v)", inPCIAddrs)
		return inPCIAddrs, nil
	}

	msg := fmt.Sprintf("vmd detected, processing addresses (input %v, existing %v)",
		inPCIAddrs, bdevCache.Controllers)

	dl, err := substVMDAddrs(inPCIAddrs, bdevCache.Controllers)
	if err != nil {
		return nil, errors.Wrapf(err, msg)
	}
	log.Debugf("%s: new %s", msg, dl)

	return dl, nil
}

// DetectVMD returns whether VMD devices have been found and a slice of VMD
// PCI addresses if found. Implements vmdDetectFn.
func DetectVMD() (*hardware.PCIAddressSet, error) {
	distro := system.GetDistribution()
	var lspciCmd *exec.Cmd

	// Check available VMD devices with command:
	// "$lspci | grep  -i -E "Volume Management Device"
	switch {
	case distro.ID == "opensuse-leap" || distro.ID == "opensuse" || distro.ID == "sles":
		lspciCmd = exec.Command("/sbin/lspci")
	default:
		lspciCmd = exec.Command("lspci")
	}

	vmdCmd := exec.Command("grep", "-i", "-E", "Volume Management Device")
	var cmdOut bytes.Buffer
	var prefixIncluded bool

	vmdCmd.Stdin, _ = lspciCmd.StdoutPipe()
	vmdCmd.Stdout = &cmdOut
	_ = lspciCmd.Start()
	_ = vmdCmd.Run()
	_ = lspciCmd.Wait()

	if cmdOut.Len() == 0 {
		return hardware.NewPCIAddressSet()
	}

	vmdCount := bytes.Count(cmdOut.Bytes(), []byte("0000:"))
	if vmdCount == 0 {
		// sometimes the output may not include "0000:" prefix
		// usually when multiple devices are in PCI_ALLOWED
		vmdCount = bytes.Count(cmdOut.Bytes(), []byte("Volume"))
	} else {
		prefixIncluded = true
	}
	vmdAddrs := make([]string, 0, vmdCount)

	i := 0
	scanner := bufio.NewScanner(&cmdOut)
	for scanner.Scan() {
		if i == vmdCount {
			break
		}
		s := strings.Split(scanner.Text(), " ")
		if !prefixIncluded {
			s[0] = "0000:" + s[0]
		}
		vmdAddrs = append(vmdAddrs, strings.TrimSpace(s[0]))
		i++
	}

	if len(vmdAddrs) == 0 {
		return nil, errors.New("error parsing cmd output")
	}

	return hardware.NewPCIAddressSet(vmdAddrs...)
}

// vmdFilterAddresses takes an input request and a list of discovered VMD addresses.
// The VMD addresses are validated against the input request allow and block lists.
// The output allow list will only contain VMD addresses if either both input allow
// and block lists are empty or if included in allow and not included in block lists.
func vmdFilterAddresses(log logging.Logger, inReq *storage.BdevPrepareRequest, vmdPCIAddrs *hardware.PCIAddressSet) (allow, block *hardware.PCIAddressSet, err error) {
	var inAllowList, inBlockList *hardware.PCIAddressSet

	inAllowList, err = hardware.NewPCIAddressSetFromString(inReq.PCIAllowList)
	if err != nil {
		return
	}
	inBlockList, err = hardware.NewPCIAddressSetFromString(inReq.PCIBlockList)
	if err != nil {
		return
	}

	// Convert any VMD backing device addresses to endpoint addresses as the input vmdPCIAddrs
	// are what we are using for filters and these are VMD endpoint addresses. This imposes a
	// limitation in that individual backing devices cannot be allowed or blocked independently.
	inAllowList, err = inAllowList.BackingToVMDAddresses()
	if err != nil {
		return
	}
	inBlockList, err = inBlockList.BackingToVMDAddresses()
	if err != nil {
		return
	}

	// Add VMD addresses to output allow list if included in request allow list.
	if inAllowList.IsEmpty() {
		allow = vmdPCIAddrs
	} else {
		allow = inAllowList.Intersect(vmdPCIAddrs)
	}

	// Remove blocked VMD addresses from block list, leaving unrecognized addresses.
	block = inBlockList.Difference(allow)
	// Remove blocked VMD addresses from allow list.
	allow = allow.Difference(inBlockList)

	return
}

// updatePrepareRequest determines if VMD devices are going to be used and updates the
// bdev prepare request with the VMD addresses explicitly set in PCI_ALLOWED list.
//
// If VMD is requested but all endpoints filtered, debug log messages are generated.
func updatePrepareRequest(log logging.Logger, req *storage.BdevPrepareRequest, vmdDetect vmdDetectFn) error {
	if !req.EnableVMD {
		return nil
	}

	vmdPCIAddrs, err := vmdDetect()
	if err != nil {
		return errors.Wrap(err, "vmd detection")
	}

	if vmdPCIAddrs.IsEmpty() {
		log.Debug("no vmd devices found")
		req.EnableVMD = false
		return nil
	}
	log.Debugf("volume management devices found: %v", vmdPCIAddrs)

	allowList, blockList, err := vmdFilterAddresses(log, req, vmdPCIAddrs)
	if err != nil {
		return errors.Wrap(err, "vmd address filtering")
	}

	if allowList.IsEmpty() {
		// No VMD domains left after filtering, log explanation and disable VMD in request.
		log.Debugf("vmd not prepared: %v domains all filtered out: allowed %v, blocked %v",
			vmdPCIAddrs, req.PCIAllowList, req.PCIBlockList)
		req.EnableVMD = false
	} else {
		log.Debugf("volume management devices selected: %v", allowList)
		req.PCIAllowList = allowList.String()
		// Retain block list in request to cater for the case where NVMe SSDs are being
		// protected against unbinding so they can continue to be used via kernel driver.
		req.PCIBlockList = blockList.String()
	}

	return nil
}
