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
package main

import (
	"bufio"
	"bytes"
	"flag"
	"io/ioutil"
	"testing"

	"github.com/google/go-cmp/cmp"
)

var update = flag.Bool("update", false, "update dmg manpage")

func TestDmg_ManPageIsCurrent(t *testing.T) {

	var manBytes bytes.Buffer
	goldenPath := "../../../../doc/man/man8/dmg.8"

	writeManPage(bufio.NewWriter(&manBytes))
	if *update {
		err := ioutil.WriteFile(goldenPath, manBytes.Bytes(), 0644)
		if err != nil {
			t.Error("failed to updated dmg manpage")
		}
	}

	goldenBytes, err := ioutil.ReadFile(goldenPath)
	if err != nil {
		t.Error("failed to read dmg manpage")
	}

	if bytes.Compare(manBytes.Bytes(), goldenBytes) != 0 {
		t.Errorf("dmg manpage does not match expected output. If this is intentional rerun this test with -args --update\n Diff:\n%s", cmp.Diff(goldenBytes, manBytes.Bytes()))
	}
}
