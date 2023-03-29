//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
)

const (
	// Minimum amount of hugepage memory (in bytes) needed for each target.
	memHugepageMinPerTarget = 1 << 30 // 1GiB
	// Default amount of memory reserved for system when calculating tmpfs capacity for SCM.
	memSysDefRsvd = 6 << 30 // 6GiB
	// Default amount of memory reserved per-engine when calculating tmpfs capacity for SCM.
	memEngineDefRsvd = 1 << 30 // 1GiB
	// MemTmpfsMin is t2e minimum amount of memory needed for each engine's tmpfs SCM.
	MemTmpfsMin = 4 << 30 // 4GiB
)

type GetMemInfoFn func() (*MemInfo, error)

// MemInfo contains information about system hugepages.
type MemInfo struct {
	HugepagesTotal  int `json:"hugepages_total"`
	HugepagesFree   int `json:"hugepages_free"`
	HugepagesRsvd   int `json:"hugepages_rsvd"`
	HugepagesSurp   int `json:"hugepages_surp"`
	HugepageSizeKiB int `json:"hugepage_size_kb"`
	MemTotalKiB     int `json:"mem_total"`
	MemFreeKiB      int `json:"mem_free"`
	MemAvailableKiB int `json:"mem_available"`
}

func (mi *MemInfo) HugepagesTotalMB() int {
	return (mi.HugepagesTotal * mi.HugepageSizeKiB) / 1024
}

func (mi *MemInfo) HugepagesFreeMB() int {
	return (mi.HugepagesFree * mi.HugepageSizeKiB) / 1024
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
				parseInt(sf[0], &mi.HugepageSizeKiB)
			case "MemTotal":
				parseInt(sf[0], &mi.MemTotalKiB)
			case "MemFree":
				parseInt(sf[0], &mi.MemFreeKiB)
			case "MemAvailable":
				parseInt(sf[0], &mi.MemAvailableKiB)
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
	minHugeMem := memHugepageMinPerTarget * numTargets

	return minHugeMem / hugepageSizeBytes, nil
}

// CalcScmSi2e returns recommended SCM RAM-disk size calculated as
// (total mem - hugepage mem - sys rsvd mem - (engine rsvd mem * nr engines)) / nr engines.
// All values in units of bytes and return value is for a single RAM-disk/engine.
func CalcScmSize(log logging.Logger, memTot, memHuge, rsvSys, rsvEng uint64, engCount int) (uint64, error) {
	if memTot == 0 {
		return 0, errors.New("CalcScmSize() requires nonzero total mem")
	}
	if engCount == 0 {
		return 0, errors.New("CalcScmSize() requires nonzero nr engines")
	}

	var memSys uint64 = memSysDefRsvd
	if rsvSys > 0 {
		memSys = rsvSys
	}
	var memEng uint64 = memEngineDefRsvd
	if rsvEng > 0 {
		memEng = rsvEng
	}

	msgStats := fmt.Sprintf("mem total: %s, mem hugepage: %s, nr engines: %d, "+
		"sys mem rsvd: %s, engine mem rsvd: %s", humanize.IBytes(memTot),
		humanize.IBytes(memHuge), engCount, humanize.IBytes(memSys),
		humanize.IBytes(memEng))

	memRsvd := memHuge + memSys + (memEng * uint64(engCount))
	if memTot < memRsvd {
		return 0, errors.Errorf("insufficient ram to meet minimum requirements (%s)",
			msgStats)
	}

	scmSize := (memTot - memRsvd) / uint64(engCount)

	log.Debugf("tmpfs scm size %s calculated using %s", humanize.IBytes(scmSize), msgStats)

	return scmSize, nil
}
