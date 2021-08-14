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
	"github.com/daos-stack/daos/src/control/server/storage"
)

// findPciAddrsWithDomain returns controllers that match the input prefix in the
// domain component of their PCI address.
func findPciAddrsWithDomain(inCtrlrs storage.NvmeControllers, prefix string) ([]string, error) {
	var outPciAddrs []string

	for _, ctrlr := range inCtrlrs {
		domain, _, _, _, err := common.ParsePCIAddress(ctrlr.PciAddr)
		if err != nil {
			return nil, err
		}
		if fmt.Sprintf("%x", domain) == prefix {
			outPciAddrs = append(outPciAddrs, ctrlr.PciAddr)
		}
	}

	return outPciAddrs, nil
}

// substVMDAddrs replaces VMD PCI addresses in input device list with the
// PCI addresses of the backing devices behind the VMD.
//
// Select any PCI addresses that have the compressed VMD address BDF as backing
// address domain.
//
// Return new device list with PCI addresses of devices behind the VMD.
func substVMDAddrs(inPCIAddrs []string, foundCtrlrs storage.NvmeControllers) ([]string, error) {
	if len(foundCtrlrs) == 0 {
		return nil, nil
	}

	var outPciAddrs []string
	for _, dev := range inPCIAddrs {
		_, b, d, f, err := common.ParsePCIAddress(dev)
		if err != nil {
			return nil, err
		}
		matchDevs, err := findPciAddrsWithDomain(foundCtrlrs,
			fmt.Sprintf("%02x%02x%02x", b, d, f))
		if err != nil {
			return nil, err
		}
		if len(matchDevs) == 0 {
			outPciAddrs = append(outPciAddrs, dev)
			continue
		}
		outPciAddrs = append(outPciAddrs, matchDevs...)
	}

	return outPciAddrs, nil
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
	// Check available VMD devices with command:
	// "$lspci | grep  -i -E "201d | Volume Management Device"
	lspciCmd := exec.Command("lspci")
	vmdCmd := exec.Command("grep", "-i", "-E", "201d|Volume Management Device")
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
		if vmdCount == 0 {
			vmdCount = bytes.Count(cmdOut.Bytes(), []byte("201d"))
		}
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

	if req.PCIAllowList != "" && vmdReq.PCIAllowList == "" {
		log.Debugf("vmd prep: %v devices not allowed", vmdPCIAddrs)
		return nil, nil
	}
	if req.PCIBlockList != "" && vmdReq.PCIAllowList == "" {
		log.Debugf("vmd prep: %v devices blocked", vmdPCIAddrs)
		return nil, nil
	}
	log.Debugf("volume management devices selected: %v", vmdReq.PCIAllowList)

	return &vmdReq, nil
}
