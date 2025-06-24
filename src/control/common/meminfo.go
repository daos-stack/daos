//
// (C) Copyright 2019-2023 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
)

var (
	pathRootProc  = "/proc"
	pathRootSys   = "/sys"
	regexpNodeDir = regexp.MustCompile(`node\d+$`)
	regexpNodeTxt = regexp.MustCompile(`Node \d+ `)
)

// GetMemInfoFn is an alias for a function that returns memory information.
type GetMemInfoFn func() (*MemInfo, error)

// MemInfoT contains system memory details gathered from meminfo files.
type MemInfoT struct {
	NumaNodeIndex   int `json:"numa_node_index"`
	HugepagesTotal  int `json:"hugepages_total" hash:"ignore"`
	HugepagesFree   int `json:"hugepages_free" hash:"ignore"`
	HugepagesRsvd   int `json:"hugepages_reserved" hash:"ignore"`
	HugepagesSurp   int `json:"hugepages_surplus" hash:"ignore"`
	HugepageSizeKiB int `json:"hugepage_size_kb"`
	MemTotalKiB     int `json:"mem_total_kb"`
	MemFreeKiB      int `json:"mem_free_kb" hash:"ignore"`
	MemAvailableKiB int `json:"mem_available_kb" hash:"ignore"`
	MemUsedKiB      int `json:"mem_used_kb" hash:"ignore"`
}

// MemInfo contains information about system memory.
type MemInfo struct {
	MemInfoT             // Overall info.
	NumaNodes []MemInfoT // Per-NUMA-node info.
}

// Summary reports basic total system memory stats.
func (mi *MemInfo) Summary() string {
	if mi == nil {
		return "<nil>"
	}

	var msgsHuge []string
	for _, nn := range mi.NumaNodes {
		msgsHuge = append(msgsHuge, fmt.Sprintf("node-%d total/free: %d/%d", nn.NumaNodeIndex,
			nn.HugepagesTotal, nn.HugepagesFree))
	}
	msgHuge := strings.Join(msgsHuge, ", ")

	return fmt.Sprintf("hugepage size: %s, %smem total/free/available: %s/%s/%s",
		humanize.IBytes(uint64(mi.HugepageSizeKiB*humanize.KiByte)), msgHuge,
		humanize.IBytes(uint64(mi.MemTotalKiB*humanize.KiByte)),
		humanize.IBytes(uint64(mi.MemFreeKiB*humanize.KiByte)),
		humanize.IBytes(uint64(mi.MemAvailableKiB*humanize.KiByte)))
}

// HugepagesTotalMB reports total hugepage memory for a system calculated from default size
// hugepages.
func (mi *MemInfo) HugepagesTotalMB() int {
	if mi == nil {
		return 0
	}
	return (mi.HugepagesTotal * mi.HugepageSizeKiB) / 1024
}

// HugepagesFreeMB reports free hugepage memory for a system calculated from default size hugepages.
func (mi *MemInfo) HugepagesFreeMB() int {
	if mi == nil {
		return 0
	}
	return (mi.HugepagesFree * mi.HugepageSizeKiB) / 1024
}

func parseInt(a string, i *int) {
	v, err := strconv.Atoi(strings.TrimSpace(a))
	if err != nil {
		return
	}
	*i = v
}

func processNodeLine(nodeStr, txt string, mi *MemInfoT) (string, error) {
	idStr := strings.Split(nodeStr, " ")[1]
	id, err := strconv.Atoi(idStr)
	if err != nil {
		return "", err
	}

	if mi.NumaNodeIndex == -1 {
		mi.NumaNodeIndex = id
	} else if mi.NumaNodeIndex != id {
		return "", errors.New("unexpected mix of node ids in meminfo file")
	}

	return strings.Replace(txt, nodeStr, "", 1), nil
}

func parseMemInfoT(input io.Reader) (*MemInfoT, error) {
	mi := new(MemInfoT)

	mi.NumaNodeIndex = -1

	scn := bufio.NewScanner(input)
	for scn.Scan() {
		txt := scn.Text()

		nodeStr := regexpNodeTxt.FindString(txt)
		if nodeStr != "" {
			txtNew, err := processNodeLine(nodeStr, txt, mi)
			if err != nil {
				return nil, err
			}
			txt = txtNew
		}

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
		case "Hugepagesize", "MemTotal", "MemFree", "MemAvailable", "MemUsed":
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
			case "MemUsed":
				parseInt(sf[0], &mi.MemUsedKiB)
			}
		default:
			continue
		}
	}

	return mi, scn.Err()
}

func getMemInfo(path string) (*MemInfoT, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	return parseMemInfoT(f)
}

func getMemInfoNodes() ([]MemInfoT, error) {
	path := filepath.Join(pathRootSys, "devices", "system", "node", "node*")
	matches, err := filepath.Glob(path)
	if err != nil {
		return nil, errors.Wrap(err, "filepath glob")
	}

	nodeInfos := []MemInfoT{}
	for _, match := range matches {
		f, err := os.Stat(match)
		if err != nil {
			return nil, err
		}
		if !f.IsDir() {
			continue
		}

		// Skip if directory name isn't node*.
		if !regexpNodeDir.MatchString(filepath.Base(match)) {
			continue
		}

		nodeMemInfoPath := filepath.Join(match, "meminfo")
		mi, err := getMemInfo(nodeMemInfoPath)
		if err != nil {
			return nil, err
		}
		if mi.NumaNodeIndex == -1 {
			return nil, errors.New("missing numa node id in meminfo file")
		}
		nodeInfos = append(nodeInfos, *mi)
	}

	return nodeInfos, nil
}

// GetMemInfo reads /proc/meminfo and returns information about system hugepages and memory (RAM).
// Per-NUMA-node stats are then retrieved from /sys/devices/system/node/node*/meminfo and stored
// under NumaNodes.
func GetMemInfo() (*MemInfo, error) {
	mi := new(MemInfo)

	path := filepath.Join(pathRootProc, "meminfo")
	mit, err := getMemInfo(path)
	if err != nil {
		return nil, err
	}
	mi.MemInfoT = *mit

	mi.NumaNodes, err = getMemInfoNodes()
	if err != nil {
		return nil, err
	}

	return mi, nil
}
