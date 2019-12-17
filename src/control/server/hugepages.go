//
// (C) Copyright 2019 Intel Corporation.
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
package server

import (
	"bufio"
	"io"
	"os"
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

type hugePageInfo struct {
	Total      int
	Free       int
	Reserved   int
	Surplus    int
	PageSizeKb int
}

func (hpi *hugePageInfo) TotalMB() int {
	return (hpi.Total * hpi.PageSizeKb) / 1024
}

func (hpi *hugePageInfo) FreeMB() int {
	return (hpi.Free * hpi.PageSizeKb) / 1024
}

func parseInt(a string, i *int) {
	v, err := strconv.Atoi(strings.TrimSpace(a))
	if err != nil {
		return
	}
	*i = v
}

func parseHugePageInfo(input io.Reader) (*hugePageInfo, error) {
	hpi := new(hugePageInfo)

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
			// units are always kB afaik -- double-check
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

func getHugePageInfo() (*hugePageInfo, error) {
	f, err := os.Open("/proc/meminfo")
	if err != nil {
		return nil, err
	}
	defer f.Close()

	return parseHugePageInfo(f)
}
