//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
