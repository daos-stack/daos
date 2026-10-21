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
	} {
		t.Run(name, func(t *testing.T) {
			got := newFSGetAttrJSON(tc.isDir, tc.oidStr, tc.oclass, tc.dirOclass, tc.fileOclass, tc.chunkSize)

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
