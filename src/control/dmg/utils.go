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
	"github.com/daos-stack/daos/src/control/common"
)

func hasConns(addrs client.Addresses, eMap client.ResultMap) (out string) {
	out = sprintConns(addrs, eMap)
	if len(addrs) == 0 {
		out = fmt.Sprintf("%sNo active connections!", out)
	}

	return
}

func sprintConns(addrs client.Addresses, eMap client.ResultMap) (out string) {
	// map keys always processed in order
	var keys []string
	for k := range eMap {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	for _, key := range keys {
		out = fmt.Sprintf(
			"%sfailed to connect to %s (%s)\n", out, key, eMap[key].Err)
	}

	return fmt.Sprintf("%sActive connections: %v\n", out, addrs)
}

// unpackFormat takes a map of addresses to result type and prints either
// decoded struct or provided error.
func unpackFormat(i interface{}) string {
	decoded := make(map[string]interface{})

	switch v := i.(type) {
	case client.ClientFeatureMap:
		for addr, res := range v {
			if res.Err != nil {
				decoded[addr] = res.Err.Error()
				continue
			}

			decoded[addr] = res.Fm
		}
	case client.ClientNvmeMap:
		for addr, res := range v {
			if res.Err != nil {
				decoded[addr] = res.Err.Error()
				continue
			}

			decoded[addr] = res.Ctrlrs
		}
	case client.ClientScmMap:
		for addr, res := range v {
			if res.Err != nil {
				decoded[addr] = res.Err.Error()
				continue
			}

			decoded[addr] = res.Mms
		}
	case client.ResultMap:
		for addr, res := range v {
			if res.Err != nil {
				decoded[addr] = res.Err.Error()
				continue
			}

			decoded[addr] = "Success!"
		}
	default:
		fmt.Printf("unknown format %#v\n", i)
	}

	s, err := common.StructsToString(decoded)
	if err != nil {
		return fmt.Sprintf(
			"Unable to YAML encode response for %%[1]ss! (%s)\n", err)
	}
	out := "Listing %[1]ss on connected storage servers:\n"
	return fmt.Sprintf("%s%s\n", out, s)
}
