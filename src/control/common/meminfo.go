//
// (C) Copyright 2019-2023 Intel Corporation.
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
	// minHugepageMemPerTarget is the minimum amount of hugepage memory (in bytes) needed
	// for each target.
	minHugepageMemPerTarget = 1 << 30 // 1GiB
)

// MemInfo contains information about system hugepages.
type MemInfo struct {
	HugepagesTotal int `json:"hugepages_total"`
	HugepagesFree  int `json:"hugepages_free"`
	HugepagesRsvd  int `json:"hugepages_rsvd"`
	HugepagesSurp  int `json:"hugepages_surp"`
	HugepageSizeKb int `json:"hugepage_size_kb"`
	MemTotal       int `json:"mem_total"`
	MemFree        int `json:"mem_free"`
	MemAvailable   int `json:"mem_available"`
}

func (mi *MemInfo) HugepagesTotalMB() int {
	return (mi.HugepagesTotal * mi.HugepageSizeKb) / 1024
}

func (mi *MemInfo) HugepagesFreeMB() int {
	return (mi.HugepagesFree * mi.HugepageSizeKb) / 1024
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
			parseInt(keyVal[1], &mi.HugepagesTotal)
		case "HugePages_Free":
			parseInt(keyVal[1], &mi.HugepagesFree)
		case "HugePages_Rsvd":
			parseInt(keyVal[1], &mi.HugepagesRsvd)
		case "HugePages_Surp":
			parseInt(keyVal[1], &mi.HugepagesSurp)
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
				parseInt(sf[0], &mi.HugepageSizeKb)
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

// CalcMinHugepages returns the minimum number of hugepages that should be
// requested for the given number of targets.
func CalcMinHugepages(hugepageSizeKb int, numTargets int) (int, error) {
	if numTargets < 1 {
		return 0, errors.New("numTargets must be > 0")
	}

	hugepageSizeBytes := hugepageSizeKb * humanize.KiByte // KiB to B
	if hugepageSizeBytes == 0 {
		return 0, errors.New("invalid system hugepage size")
	}
	minHugepageBytes := minHugepageMemPerTarget * numTargets

	return minHugepageBytes / hugepageSizeBytes, nil
}
