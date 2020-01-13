//
// (C) Copyright 2020 Intel Corporation.
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

package common

import (
	"testing"
)

func TestUtils_HasPort(t *testing.T) {
	for name, tc := range map[string]struct {
		addr   string
		expRes bool
	}{
		"host has port": {"localhost:10001", true},
		"host no port":  {"localhost", false},
		"ip has port":   {"192.168.1.1:10001", true},
		"ip no port":    {"192.168.1.1", false},
	} {
		t.Run(name, func(t *testing.T) {
			AssertEqual(t, tc.expRes, HasPort(tc.addr), name)
		})
	}
}
