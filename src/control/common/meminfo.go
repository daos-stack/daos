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

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
)

type GetMemInfoFn func() (*MemInfo, error)

const (
	// MinTargetHugePageSize is the minimum amount of hugepage space that
	// can be requested for each target.
	MinTargetHugePageSize = 1 << 30 // 1GiB
	// ExtraHugePages is the number of extra hugepages to request beyond
	// the minimum required, often one or two are not reported as available.
	ExtraHugePages = 2
)

// MemInfo contains information about system hugepages.
type MemInfo struct {
	HugePagesTotal int `json:"hugepages_total"`
	HugePagesFree  int `json:"hugepages_free"`
	HugePagesRsvd  int `json:"hugepages_rsvd"`
	HugePagesSurp  int `json:"hugepages_surp"`
	HugePageSizeKb int `json:"hugepage_size_kb"`
	MemTotal       int `json:"mem_total"`
	MemFree        int `json:"mem_free"`
	MemAvailable   int `json:"mem_available"`
}

func (mi *MemInfo) HugePagesTotalMB() int {
	return (mi.HugePagesTotal * mi.HugePageSizeKb) / 1024
}

func (mi *MemInfo) HugePagesFreeMB() int {
	return (mi.HugePagesFree * mi.HugePageSizeKb) / 1024
}

func parseInt(a string, i *int) {
	v, err := strconv.Atoi(strings.TrimSpace(a))
	if err != nil {
		return
	}
	*i = v
}

func parseMemInfo(input io.Reader) (*MemInfo, error) {
	mi := new(MemInfo)

	scn := bufio.NewScanner(input)
	for scn.Scan() {
		txt := scn.Text()
		keyVal := strings.Split(txt, ":")
		if len(keyVal) < 2 {
			continue
		}

		switch keyVal[0] {
		case "HugePages_Total":
			parseInt(keyVal[1], &mi.HugePagesTotal)
		case "HugePages_Free":
			parseInt(keyVal[1], &mi.HugePagesFree)
		case "HugePages_Rsvd":
			parseInt(keyVal[1], &mi.HugePagesRsvd)
		case "HugePages_Surp":
			parseInt(keyVal[1], &mi.HugePagesSurp)
		case "Hugepagesize", "MemTotal", "MemFree", "MemAvailable":
			sf := strings.Fields(keyVal[1])
			if len(sf) != 2 {
				return nil, errors.Errorf("unable to parse %q", keyVal[1])
			}
			// units are hard-coded to kB in the kernel, but doesn't hurt
			// to double-check...
			if sf[1] != "kB" {
				return nil, errors.Errorf("unhandled size unit %q", sf[1])
			}

			switch keyVal[0] {
			case "Hugepagesize":
				parseInt(sf[0], &mi.HugePageSizeKb)
			case "MemTotal":
				parseInt(sf[0], &mi.MemTotal)
			case "MemFree":
				parseInt(sf[0], &mi.MemFree)
			case "MemAvailable":
				parseInt(sf[0], &mi.MemAvailable)
			}
		default:
			continue
		}
	}

	return mi, scn.Err()
}

// GetMemInfo reads /proc/meminfo and returns information about
// system hugepages and available memory (RAM).
func GetMemInfo() (*MemInfo, error) {
	f, err := os.Open("/proc/meminfo")
	if err != nil {
		return nil, err
	}
	defer f.Close()

	return parseMemInfo(f)
}

// CalcMinHugePages returns the minimum number of hugepages that should be
// requested for the given number of targets.
func CalcMinHugePages(hugePageSizeKb int, numTargets int) (int, error) {
	if numTargets < 1 {
		return 0, errors.New("numTargets must be > 0")
	}

	hugepageSizeBytes := hugePageSizeKb * humanize.KiByte // KiB to B
	if hugepageSizeBytes == 0 {
		return 0, errors.New("invalid system hugepage size")
	}
	minHugePageBytes := MinTargetHugePageSize * numTargets

	return minHugePageBytes / hugepageSizeBytes, nil
}
