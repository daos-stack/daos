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

package main

import (
	"fmt"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
)

func defaultMockControlService(t *testing.T) *controlService {
	c := defaultMockConfig(t)
	return mockControlService(&c)
}

func mockControlService(config *configuration) *controlService {
	cs := controlService{
		nvme:   defaultMockNvmeStorage(config),
		scm:    defaultMockScmStorage(config),
		config: config,
	}

	return &cs
}

func TestFormatScmStorage(t *testing.T) {
	tests := []struct {
		mountRet   error
		unmountRet error
		mkdirRet   error
		removeRet  error
		mount      string
		class      ScmClass
		devs       []string
		size       int
		desc       string
		errMsg     string
	}{
		{
			desc:  "ram success",
			mount: "/mnt/daos",
			class: scmRAM,
			size:  6,
		},
		{
			desc:  "dcpm success",
			mount: "/mnt/daos",
			class: scmDCPM,
			devs:  []string{"/dev/pmem1"},
		},
	}

	serverIdx := 0

	for _, tt := range tests {
		config := newMockScmConfig(
			tt.mountRet, tt.unmountRet, tt.mkdirRet, tt.removeRet,
			tt.mount, tt.class, tt.devs, tt.size)

		config.FormatOverride = false

		cs := mockControlService(config)
		cs.Setup() // set cond var locked

		c := cs.config.Servers[serverIdx].FormatCond

		go func() {
			// should wait for lock to be released on main thread
			// then signal to unlock once format completed
			if _, err := cs.FormatStorage(nil, nil); err != nil {
				// for purposes of test, signal cond on fail
				c.L.Lock()
				c.Broadcast()
				c.L.Unlock()
				t.Fatal(fmt.Sprintf("%+v", err))
			}
		}()

		AssertEqual(t, cs.nvme.formatted, false, tt.desc)
		AssertEqual(t, cs.scm.formatted, false, tt.desc)

		c.Wait()

		AssertEqual(t, cs.nvme.formatted, true, tt.desc)
		AssertEqual(t, cs.scm.formatted, true, tt.desc)
	}
}
