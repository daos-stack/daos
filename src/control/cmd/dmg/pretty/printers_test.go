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

package pretty

import (
	"errors"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestControl_PrintHostErrorsMap(t *testing.T) {
	makeHosts := func(hosts ...string) []string {
		return hosts
	}
	makeErrors := func(errStrings ...string) (errs []error) {
		for _, errStr := range errStrings {
			errs = append(errs, errors.New(errStr))
		}
		return
	}

	for name, tc := range map[string]struct {
		hosts       []string
		errors      []error
		expPrintStr string
	}{
		"one host one error": {
			hosts:  makeHosts("host1"),
			errors: makeErrors("whoops"),
			expPrintStr: `
Hosts Error  
----- -----  
host1 whoops 
`,
		},
		"two hosts one error": {
			hosts:  makeHosts("host1", "host2"),
			errors: makeErrors("whoops", "whoops"),
			expPrintStr: `
Hosts     Error  
-----     -----  
host[1-2] whoops 
`,
		},
		"two hosts one error (sorted)": {
			hosts:  makeHosts("host2", "host1"),
			errors: makeErrors("whoops", "whoops"),
			expPrintStr: `
Hosts     Error  
-----     -----  
host[1-2] whoops 
`,
		},
		"two hosts two errors": {
			hosts:  makeHosts("host1", "host2"),
			errors: makeErrors("whoops", "oops"),
			expPrintStr: `
Hosts Error  
----- -----  
host1 whoops 
host2 oops   
`,
		},
		"two hosts same port one error": {
			hosts:  makeHosts("host1:1", "host2:1"),
			errors: makeErrors("whoops", "whoops"),
			expPrintStr: `
Hosts       Error  
-----       -----  
host[1-2]:1 whoops 
`,
		},
		"two hosts different port one error": {
			hosts:  makeHosts("host1:1", "host2:2"),
			errors: makeErrors("whoops", "whoops"),
			expPrintStr: `
Hosts           Error  
-----           -----  
host1:1,host2:2 whoops 
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			hem := make(control.HostErrorsMap)
			for i, host := range tc.hosts {
				if err := hem.Add(host, tc.errors[i]); err != nil {
					t.Fatal(err)
				}
			}

			var bld strings.Builder
			if err := PrintHostErrorsMap(hem, &bld, PrintWithHostPorts()); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}
