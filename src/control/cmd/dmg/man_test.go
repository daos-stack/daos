//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package main

import (
	"bytes"
	"flag"
	"fmt"
	"io/ioutil"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

var update = flag.Bool("update", false, "update dmg manpage")

func TestDmg_ManPageIsCurrent(t *testing.T) {
	var manBytes bytes.Buffer
	goldenPath := "../../../../doc/man/man8/dmg.8"

	stripDate := func(in []byte) []byte {
		var out bytes.Buffer
		for _, line := range strings.Split(string(in), "\n") {
			if !strings.HasPrefix(line, ".TH dmg 1") {
				fmt.Fprintln(&out, line)
			}
		}
		return out.Bytes()
	}

	writeManPage(&manBytes)
	if *update {
		err := ioutil.WriteFile(goldenPath, manBytes.Bytes(), 0644)
		if err != nil {
			t.Error("failed to updated dmg manpage")
		}
	}
	strippedGenerated := stripDate(manBytes.Bytes())

	goldenBytes, err := ioutil.ReadFile(goldenPath)
	if err != nil {
		t.Error("failed to read dmg manpage")
	}
	strippedGolden := stripDate(goldenBytes)

	// TODO: Figure out how to find this dynamically
	pkgPath := "github.com/daos-stack/daos/src/control/cmd/dmg"
	if diff := cmp.Diff(strippedGolden, strippedGenerated); diff != "" {
		t.Fatalf("%s is out of date.\nRun `go test %s -run %s -args --update` to update it and then commit it.", goldenPath, pkgPath, t.Name())
	}
}
