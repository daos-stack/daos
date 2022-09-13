//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"bufio"
	"io"
	"os"
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

type GetHugePageInfoFn func() (*HugePageInfo, error)

const (
	// MinTargetHugePageSize is the minimum amount of hugepage space that
	// can be requested for each target.
	MinTargetHugePageSize = 1 << 30 // 1GiB
	// ExtraHugePages is the number of extra hugepages to request beyond
	// the minimum required, often one or two are not reported as available.
	ExtraHugePages = 2
)

// HugePageInfo contains information about system hugepages.
type HugePageInfo struct {
	Total      int `json:"total"`
	Free       int `json:"free"`
	Reserved   int `json:"reserved"`
	Surplus    int `json:"surplus"`
	PageSizeKb int `json:"page_size_kb"`
}

func (hpi *HugePageInfo) TotalMB() int {
	return (hpi.Total * hpi.PageSizeKb) / 1024
}

func (hpi *HugePageInfo) FreeMB() int {
	return (hpi.Free * hpi.PageSizeKb) / 1024
}

func parseInt(a string, i *int) {
	v, err := strconv.Atoi(strings.TrimSpace(a))
	if err != nil {
		return
	}
	*i = v
}

func parseHugePageInfo(input io.Reader) (*HugePageInfo, error) {
	hpi := new(HugePageInfo)

	scn := bufio.NewScanner(input)
	for scn.Scan() {
		keyVal := strings.Split(scn.Text(), ":")
		if len(keyVal) < 2 {
			continue
		}

		switch keyVal[0] {
		case "HugePages_Total":
			parseInt(keyVal[1], &hpi.Total)
		case "HugePages_Free":
			parseInt(keyVal[1], &hpi.Free)
		case "HugePages_Rsvd":
			parseInt(keyVal[1], &hpi.Reserved)
		case "HugePages_Surp":
			parseInt(keyVal[1], &hpi.Surplus)
		case "Hugepagesize":
			sf := strings.Fields(keyVal[1])
			if len(sf) != 2 {
				return nil, errors.Errorf("unable to parse %q", keyVal[1])
			}
			// units are hard-coded to kB in the kernel, but doesn't hurt
			// to double-check...
			if sf[1] != "kB" {
				return nil, errors.Errorf("unhandled page size unit %q", sf[1])
			}
			parseInt(sf[0], &hpi.PageSizeKb)
		default:
			continue
		}
	}

	return hpi, scn.Err()
}

// GetHugePageInfo reads /proc/meminfo and returns information about
// system hugepages.
func GetHugePageInfo() (*HugePageInfo, error) {
	f, err := os.Open("/proc/meminfo")
	if err != nil {
		return nil, err
	}
	defer f.Close()

	return parseHugePageInfo(f)
}

// CalcMinHugePages returns the minimum number of hugepages that should be
// requested for the given number of targets.
func CalcMinHugePages(hugePageSizeKb int, numTargets int) (int, error) {
	if numTargets < 1 {
		return 0, errors.New("numTargets must be > 0")
	}

	hugepageSizeBytes := hugePageSizeKb << 10 // KiB to B
	if hugepageSizeBytes == 0 {
		return 0, errors.New("invalid system hugepage size")
	}
	minHugePageBytes := MinTargetHugePageSize * numTargets

	return minHugePageBytes / hugepageSizeBytes, nil
}
