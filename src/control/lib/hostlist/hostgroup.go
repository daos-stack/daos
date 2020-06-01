//
// (C) Copyright 2019-2020 Intel Corporation.
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
package hostlist

import (
	"bytes"
	"fmt"
	"sort"
)

// HostGroups maps a set of hosts to a string key value.
type HostGroups map[string]*HostSet

func (hg HostGroups) Keys() []string {
	keys := make([]string, 0, len(hg))

	for key := range hg {
		keys = append(keys, key)
	}

	sort.Strings(keys)
	return keys
}

func (hg HostGroups) AddHost(key, host string) error {
	if _, exists := hg[key]; !exists {
		hg[key] = new(HostSet)
	}

	_, err := hg[key].Insert(host)
	return err
}

func (hg HostGroups) String() string {
	var buf bytes.Buffer

	padding := 0
	keys := hg.Keys()
	for _, key := range keys {
		valStr := hg[key].String()
		if len(valStr) > padding {
			padding = len(valStr)
		}
	}

	for _, key := range hg.Keys() {
		fmt.Fprintf(&buf, "%*s: %s\n", padding, hg[key], key)
	}

	return buf.String()
}
