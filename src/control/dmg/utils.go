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
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
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

// annotateState adds status string representation if no Info provided
func annotateState(state *pb.ResponseState) {
	if state.Info == "" {
		state.Info = fmt.Sprintf(
			"status=%s",
			state.Status.String())
	}
}

// unpackClientMap takes a map of addresses to result type and prints either
// decoded struct or provided error.
func unpackClientMap(i interface{}) string {
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
	case client.ClientCtrlrMap:
		for addr, res := range v {
			if res.Err != nil {
				decoded[addr] = res.Err.Error()
			} else if len(res.Ctrlrs) > 0 {
				decoded[addr] = res.Ctrlrs
			} else {
				for i := range res.Responses {
					annotateState(res.Responses[i].State)
				}
				decoded[addr] = res.Responses
			}
		}
	case client.ClientModuleMap:
		for addr, res := range v {
			if res.Err != nil {
				decoded[addr] = res.Err.Error()
			} else if len(res.Modules) > 0 {
				decoded[addr] = res.Modules
			} else {
				for i := range res.Responses {
					annotateState(res.Responses[i].State)
				}
				decoded[addr] = res.Responses
			}
		}
	case client.ClientMountMap:
		for addr, res := range v {
			if res.Err != nil {
				decoded[addr] = res.Err.Error()
			} else if len(res.Mounts) > 0 {
				decoded[addr] = res.Mounts
			} else {
				for i := range res.Responses {
					annotateState(res.Responses[i].State)
				}
				decoded[addr] = res.Responses
			}
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
