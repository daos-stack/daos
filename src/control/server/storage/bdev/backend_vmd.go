//
// (C) Copyright 2021-2022 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// getVMD returns VMD endpoint address when provided string is a VMD backing device PCI address.
// If the input string is not a VMD backing device PCI address, common.ErrNotVMDBackingAddress
// is returned.
func getVMD(inAddr string) (*common.PCIAddress, error) {
	addr, err := common.NewPCIAddress(inAddr)
	if err != nil {
		return nil, errors.Wrap(err, "controller pci address invalid")
	}

	return addr.BackingToVMDAddress()
}

// mapVMDToBackingDevs stores found vmd backing device details under vmd address key.
func mapVMDToBackingDevs(foundCtrlrs storage.NvmeControllers) (map[string]storage.NvmeControllers, error) {
	vmds := make(map[string]storage.NvmeControllers)

	for _, ctrlr := range foundCtrlrs {
		vmdAddr, err := getVMD(ctrlr.PciAddr)
		if err != nil {
			if err == common.ErrNotVMDBackingAddress {
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
func mapVMDToBackingAddrs(foundCtrlrs storage.NvmeControllers) (map[string]*common.PCIAddressSet, error) {
	vmds := make(map[string]*common.PCIAddressSet)

	for _, ctrlr := range foundCtrlrs {
		vmdAddr, err := getVMD(ctrlr.PciAddr)
		if err != nil {
			if err == common.ErrNotVMDBackingAddress {
				continue
			}
			return nil, err
		}

		if _, exists := vmds[vmdAddr.String()]; !exists {
			vmds[vmdAddr.String()] = new(common.PCIAddressSet)
		}

		// add backing device address to vmd address key in map
		if err := vmds[vmdAddr.String()].AddStrings(ctrlr.PciAddr); err != nil {
			return nil, err
		}
	}

	return vmds, nil
}

// substVMDAddrs replaces VMD PCI addresses in input device list with the PCI
// addresses of the backing devices behind the VMD.
//
// Return new device list with PCI addresses of devices behind the VMD.
func substVMDAddrs(inPCIAddrs *common.PCIAddressSet, foundCtrlrs storage.NvmeControllers) (*common.PCIAddressSet, error) {
	if len(foundCtrlrs) == 0 {
		return nil, nil
	}

	vmds, err := mapVMDToBackingAddrs(foundCtrlrs)
	if err != nil {
		return nil, err
	}

	// swap input vmd addresses with respective backing addresses
	outPCIAddrs := new(common.PCIAddressSet)
	for _, inAddr := range inPCIAddrs.Addresses() {
		toAdd := []*common.PCIAddress{inAddr}

		if backing, exists := vmds[inAddr.String()]; exists {
			toAdd = backing.Addresses()
		}

		if err := outPCIAddrs.Add(toAdd...); err != nil {
			return nil, err
		}
	}

	return outPCIAddrs, nil
}

// substituteVMDAddresses wraps around substVMDAddrs and takes a BdevScanResponse
// reference along with a logger.
func substituteVMDAddresses(log logging.Logger, inPCIAddrs *common.PCIAddressSet, bdevCache *storage.BdevScanResponse) (*common.PCIAddressSet, error) {
	if bdevCache == nil || len(bdevCache.Controllers) == 0 {
		log.Debugf("no bdev cache to find vmd backing devices (devs: %v)", inPCIAddrs)
		return nil, nil
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
func DetectVMD() (*common.PCIAddressSet, error) {
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
		return common.NewPCIAddressSet()
	}

	vmdCount := bytes.Count(cmdOut.Bytes(), []byte("0000:"))
	if vmdCount == 0 {
		// sometimes the output may not include "0000:" prefix
		// usually when muliple devices are in PCI_ALLOWED
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

	return common.NewPCIAddressSet(vmdAddrs...)
}

// vmdFilterAddresses takes an input request and a list of discovered VMD addresses.
// The VMD addresses are validated against the input request allow and block lists.
// The output allow list will only contain VMD addresses if either both input allow
// and block lists are empty or if included in allow and not included in block lists.
func vmdFilterAddresses(inReq *storage.BdevPrepareRequest, vmdPCIAddrs *common.PCIAddressSet) (*storage.BdevPrepareRequest, error) {
	outAllowList := new(common.PCIAddressSet)
	outReq := *inReq

	inAllowList, err := common.NewPCIAddressSetFromString(inReq.PCIAllowList)
	if err != nil {
		return nil, err
	}
	inBlockList, err := common.NewPCIAddressSetFromString(inReq.PCIBlockList)
	if err != nil {
		return nil, err
	}

	// Set allow list to all VMD addresses if no allow or block lists in request.
	if inAllowList.IsEmpty() && inBlockList.IsEmpty() {
		outReq.PCIAllowList = vmdPCIAddrs.String()
		outReq.PCIBlockList = ""
		return &outReq, nil
	}

	// Add VMD addresses to output allow list if included in request allow list.
	if !inAllowList.IsEmpty() {
		inclAddrs := inAllowList.Intersect(vmdPCIAddrs)

		if inclAddrs.IsEmpty() {
			// no allowed vmd addresses
			outReq.PCIAllowList = ""
			outReq.PCIBlockList = ""
			return &outReq, nil
		}

		outAllowList = inclAddrs
	}

	if !inBlockList.IsEmpty() {
		// use outAllowList in case vmdPCIAddrs list has already been filtered
		inList := outAllowList

		if inList.IsEmpty() {
			inList = vmdPCIAddrs
		}

		exclAddrs := inList.Difference(inBlockList)

		if exclAddrs.IsEmpty() {
			// all vmd addresses are blocked
			outReq.PCIAllowList = ""
			outReq.PCIBlockList = ""
			return &outReq, nil
		}

		outAllowList = exclAddrs
	}

	outReq.PCIAllowList = outAllowList.String()
	outReq.PCIBlockList = ""
	return &outReq, nil
}

// getVMDPrepReq determines if VMD devices are going to be used and returns a
// bdev prepare request with the VMD addresses explicitly set in PCI_ALLOWED list.
//
// If VMD is not to be prepared, a nil request is returned.
func getVMDPrepReq(log logging.Logger, req *storage.BdevPrepareRequest, vmdDetect vmdDetectFn) (*storage.BdevPrepareRequest, error) {
	if !req.EnableVMD {
		return nil, nil
	}

	vmdPCIAddrs, err := vmdDetect()
	if err != nil {
		return nil, errors.Wrap(err, "VMD could not be enabled")
	}

	if vmdPCIAddrs.IsEmpty() {
		log.Debug("vmd prep: no vmd devices found")
		return nil, nil
	}
	log.Debugf("volume management devices detected: %v", vmdPCIAddrs)

	vmdReq, err := vmdFilterAddresses(req, vmdPCIAddrs)
	if err != nil {
		return nil, err
	}

	// No addrs left after filtering
	if vmdReq.PCIAllowList == "" {
		if req.PCIAllowList != "" {
			log.Debugf("vmd prep: %q devices not allowed", vmdPCIAddrs)
			return nil, nil
		}
		if req.PCIBlockList != "" {
			log.Debugf("vmd prep: %q devices blocked", vmdPCIAddrs)
			return nil, nil
		}
	}

	log.Debugf("volume management devices selected: %q", vmdReq.PCIAllowList)

	return vmdReq, nil
}
