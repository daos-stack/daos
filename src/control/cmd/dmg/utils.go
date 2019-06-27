//
// (C) Copyright 2018-2019 Intel Corporation.
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

package main

import (
	"fmt"
	"sort"

	"github.com/daos-stack/daos/src/control/client"
)

func hasConns(results client.ResultMap) (bool, string) {
	out := sprintConns(results)
	for _, res := range results {
		if res.Err == nil {
			return true, out
		}
	}

	// notify if there have been no successful connections
	return false, fmt.Sprintf("%sNo active connections!", out)
}

func sprintConns(results client.ResultMap) (out string) {
	// map keys always processed in order
	var addrs []string
	for addr := range results {
		addrs = append(addrs, addr)
	}
	sort.Strings(addrs)

	i := 0
	for _, addr := range addrs {
		if results[addr].Err != nil {
			out = fmt.Sprintf(
				"%sfailed to connect to %s (%s)\n",
				out, addr, results[addr].Err)
			continue
		}
		addrs[i] = addr
		i++
	}
	addrs = addrs[:i]

	return fmt.Sprintf("%sActive connections: %v\n", out, addrs)
}

// getConsent scans stdin for yes/no
func getConsent() bool {
	var response string

	_, err := fmt.Scanln(&response)
	if err != nil {
		fmt.Printf("Error reading input: %s\n", err)
		return false
	}

	if response == "no" {
		return false
	} else if response != "yes" {
		fmt.Println("Please type yes or no and then press enter:")
		return getConsent()
	}

	return true
}
