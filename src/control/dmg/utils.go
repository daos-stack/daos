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

func hasConnections(addrs client.Addresses, eMap client.ErrorMap) (
	out string) {

	out = sprintConns(addrs, eMap)
	if len(addrs) == 0 {
		out = fmt.Sprintf("%sNo active connections!", out)
	}
	return
}

func sprintConns(addrs client.Addresses, eMap client.ErrorMap) (
	out string) {

	// map keys always processed in order
	var keys []string
	for k := range eMap {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	for _, key := range keys {
		out = fmt.Sprintf(
			"%sfailed to connect to %s (%s)\n", out, key, eMap[key])
	}
	return fmt.Sprintf("%sActive connections: %v\n", out, addrs)
}

func checkAndFormat(i interface{}, err error) string {
	if err != nil {
		return fmt.Sprintf("Unable to retrieve %%[1]ss (%s)\n", err)
	}
	s, err := common.StructsToString(i)
	if err != nil {
		return fmt.Sprintf(
			"Unable to YAML encode response for %%[1]ss! (%s)\n", err)
	}
	out := "Listing %[1]ss on connected storage servers:\n"
	return fmt.Sprintf("%s%s\n", out, s)
}
