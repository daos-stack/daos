//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestDaos_newFSGetAttrJSON(t *testing.T) {
	for name, tc := range map[string]struct {
		isDir      bool
		oidStr     string
		oclass     string
		dirOclass  string
		fileOclass string
		chunkSize  uint64
		tails      []fsGetAttrTailJSON
		expJSON    string
	}{
		"file": {
			isDir:     false,
			oidStr:    "1.2",
			oclass:    "S1",
			chunkSize: 1048576,
			expJSON:   `{"oid":"1.2","oclass":"S1","chunk_size":1048576}`,
		},
		"file ignores dir/file oclass": {
			isDir:      false,
			oidStr:     "3.4",
			oclass:     "SX",
			dirOclass:  "RP_2G1",
			fileOclass: "EC_2P1G1",
			chunkSize:  4096,
			expJSON:    `{"oid":"3.4","oclass":"SX","chunk_size":4096}`,
		},
		"directory": {
			isDir:      true,
			oidStr:     "5.6",
			oclass:     "S1",
			dirOclass:  "RP_2G1",
			fileOclass: "EC_2P1G1",
			chunkSize:  1048576,
			expJSON: `{"object":{"oid":"5.6","oclass":"S1"},` +
				`"directory":{"dir_oclass":"RP_2G1","file_oclass":"EC_2P1G1","chunk_size":1048576}}`,
		},
		"directory distinct dir/file oclass not swapped": {
			isDir:      true,
			oidStr:     "7.8",
			oclass:     "SX",
			dirOclass:  "S1",
			fileOclass: "S2",
			chunkSize:  0,
			expJSON: `{"object":{"oid":"7.8","oclass":"SX"},` +
				`"directory":{"dir_oclass":"S1","file_oclass":"S2","chunk_size":0}}`,
		},
		"file with progressive-layout tail": {
			isDir:     false,
			oidStr:    "1.2",
			oclass:    "S1",
			chunkSize: 1048576,
			tails: []fsGetAttrTailJSON{
				{OID: "3.4", ObjClass: "EC_2P1G1", SplitOff: 4194304},
			},
			expJSON: `{"oid":"1.2","oclass":"S1","chunk_size":1048576,` +
				`"tails":[{"oid":"3.4","oclass":"EC_2P1G1","split_off":4194304}]}`,
		},
		"file with multiple progressive-layout tails": {
			isDir:     false,
			oidStr:    "1.2",
			oclass:    "S1",
			chunkSize: 1048576,
			tails: []fsGetAttrTailJSON{
				{OID: "3.4", ObjClass: "S2", SplitOff: 1048576},
				{OID: "5.6", ObjClass: "EC_2P1G1", SplitOff: 4194304},
			},
			expJSON: `{"oid":"1.2","oclass":"S1","chunk_size":1048576,"tails":[` +
				`{"oid":"3.4","oclass":"S2","split_off":1048576},` +
				`{"oid":"5.6","oclass":"EC_2P1G1","split_off":4194304}]}`,
		},
		"directory with progressive-layout tail omits oid": {
			isDir:      true,
			oidStr:     "5.6",
			oclass:     "S1",
			dirOclass:  "RP_2G1",
			fileOclass: "S1",
			chunkSize:  1048576,
			tails: []fsGetAttrTailJSON{
				{ObjClass: "EC_2P1G1", SplitOff: 4194304},
			},
			expJSON: `{"object":{"oid":"5.6","oclass":"S1"},"directory":{` +
				`"dir_oclass":"RP_2G1","file_oclass":"S1",` +
				`"file_pl_tails":[{"oclass":"EC_2P1G1","split_off":4194304}],` +
				`"chunk_size":1048576}}`,
		},
		"directory with multiple progressive-layout tails": {
			isDir:      true,
			oidStr:     "5.6",
			oclass:     "S1",
			dirOclass:  "RP_2G1",
			fileOclass: "S1",
			chunkSize:  1048576,
			tails: []fsGetAttrTailJSON{
				{ObjClass: "S2", SplitOff: 1048576},
				{ObjClass: "EC_2P1G1", SplitOff: 4194304},
			},
			expJSON: `{"object":{"oid":"5.6","oclass":"S1"},"directory":{` +
				`"dir_oclass":"RP_2G1","file_oclass":"S1","file_pl_tails":[` +
				`{"oclass":"S2","split_off":1048576},` +
				`{"oclass":"EC_2P1G1","split_off":4194304}],` +
				`"chunk_size":1048576}}`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			got := newFSGetAttrJSON(tc.isDir, tc.oidStr, tc.oclass, tc.dirOclass, tc.fileOclass, tc.chunkSize, tc.tails)

			gotBytes, err := json.Marshal(got)
			if err != nil {
				t.Fatalf("failed to marshal JSON: %v", err)
			}

			if diff := cmp.Diff(tc.expJSON, string(gotBytes)); diff != "" {
				t.Fatalf("unexpected JSON output (-want, +got)\n%s\n", diff)
			}
		})
	}
}
