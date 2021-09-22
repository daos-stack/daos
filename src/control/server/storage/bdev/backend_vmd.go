//
// (C) Copyright 2021 Intel Corporation.
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

// backingAddrToVMD converts a VMD backing devices address e.g. 5d0505:03.00.0
// to the relevant logical VMD address e.g. 0000:5d:05.5.
func backingAddrToVMD(backingAddr string) (string, bool) {
	domain, _, _, _, err := common.ParsePCIAddress(backingAddr)
	if err != nil {
		return "", false
	}
	if domain == 0 {
		return "", false
	}

	domStr := fmt.Sprintf("%x", domain)

	return fmt.Sprintf("0000:%c%c:%c%c.%c", domStr[0], domStr[1], domStr[2],
		domStr[3], domStr[5]), true
}

// mapVMDToBackingAddrs stores found vmd backing addresses under vmd address key.
func mapVMDToBackingAddrs(foundCtrlrs storage.NvmeControllers) map[string][]string {
	vmds := make(map[string][]string)

	for _, ctrlr := range foundCtrlrs {
		// find backing device addresses from vmd address
		vmdAddr, isVMDBackingAddr := backingAddrToVMD(ctrlr.PciAddr)
		if isVMDBackingAddr {
			vmds[vmdAddr] = append(vmds[vmdAddr], ctrlr.PciAddr)
		}
	}

	return vmds
}

// substVMDAddrs replaces VMD PCI addresses in input device list with the PCI
// addresses of the backing devices behind the VMD.
//
// Return new device list with PCI addresses of devices behind the VMD.
func substVMDAddrs(inPCIAddrs []string, foundCtrlrs storage.NvmeControllers) ([]string, error) {
	if len(foundCtrlrs) == 0 {
		return nil, nil
	}

	vmds := mapVMDToBackingAddrs(foundCtrlrs)

	// swap input vmd addresses with respective backing addresses
	var outPCIAddrs []string
	for _, inAddr := range inPCIAddrs {
		if backingAddrs, exists := vmds[inAddr]; exists {
			outPCIAddrs = append(outPCIAddrs, backingAddrs...)
			continue
		}
		outPCIAddrs = append(outPCIAddrs, inAddr)
	}

	return outPCIAddrs, nil
}

// substituteVMDAddresses wraps around substVMDAddrs and takes a BdevScanResponse
// reference along with a logger.
func substituteVMDAddresses(log logging.Logger, inPCIAddrs []string, bdevCache *storage.BdevScanResponse) ([]string, error) {
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

// detectVMD returns whether VMD devices have been found and a slice of VMD
// PCI addresses if found.
func detectVMD() ([]string, error) {
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
		return []string{}, nil
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

	return vmdAddrs, nil
}

// vmdProcessFilters takes an input request and a list of discovered VMD addresses.
// The VMD addresses are validated against the input request allow and block lists.
// The output allow list will only contain VMD addresses if either both input allow
// and block lists are empty or if included in allow and not included in block lists.
func vmdProcessFilters(inReq *storage.BdevPrepareRequest, vmdPCIAddrs []string) storage.BdevPrepareRequest {
	var outAllowList []string
	outReq := *inReq

	if inReq.PCIAllowList == "" && inReq.PCIBlockList == "" {
		outReq.PCIAllowList = strings.Join(vmdPCIAddrs, storage.BdevPciAddrSep)
		outReq.PCIBlockList = ""
		return outReq
	}

	if inReq.PCIAllowList != "" {
		allowed := strings.Split(inReq.PCIAllowList, storage.BdevPciAddrSep)
		for _, addr := range vmdPCIAddrs {
			if common.Includes(allowed, addr) {
				outAllowList = append(outAllowList, addr)
			}
		}
		if len(outAllowList) == 0 {
			// no allowed vmd addresses
			outReq.PCIAllowList = ""
			outReq.PCIBlockList = ""
			return outReq
		}
	}

	if inReq.PCIBlockList != "" {
		var outList []string
		inList := outAllowList // in case vmdPCIAddrs list has already been filtered
		if len(inList) == 0 {
			inList = vmdPCIAddrs
		}
		blocked := strings.Split(inReq.PCIBlockList, storage.BdevPciAddrSep)
		for _, addr := range inList {
			if !common.Includes(blocked, addr) {
				outList = append(outList, addr)
			}
		}
		outAllowList = outList
		if len(outAllowList) == 0 {
			// no allowed vmd addresses
			outReq.PCIAllowList = ""
			outReq.PCIBlockList = ""
			return outReq
		}
	}

	outReq.PCIAllowList = strings.Join(outAllowList, storage.BdevPciAddrSep)
	outReq.PCIBlockList = ""
	return outReq
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

	if len(vmdPCIAddrs) == 0 {
		log.Debug("vmd prep: no vmd devices found")
		return nil, nil
	}
	log.Debugf("volume management devices detected: %v", vmdPCIAddrs)

	vmdReq := vmdProcessFilters(req, vmdPCIAddrs)

	// No addrs left after filtering
	if vmdReq.PCIAllowList == "" {
		if req.PCIAllowList != "" {
			log.Debugf("vmd prep: %v devices not allowed", vmdPCIAddrs)
			return nil, nil
		}
		if req.PCIBlockList != "" {
			log.Debugf("vmd prep: %v devices blocked", vmdPCIAddrs)
			return nil, nil
		}
	}

	log.Debugf("volume management devices selected: %v", vmdReq.PCIAllowList)

	return &vmdReq, nil
}
