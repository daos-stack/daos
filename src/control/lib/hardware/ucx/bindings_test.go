//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build ucx
// +build ucx

package ucx

import (
	"fmt"
	"testing"
)

func TestUCXBindings_signalHandling(t *testing.T) {
	close, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	defer close()

	defer func() {
		// We would expect this to allow us to recover from the SIGSEGV
		// we'll trigger on ourselves below.
		if result := recover(); result != nil {
			fmt.Printf("successfully recovered from panic: %+v\n", result)
		}
	}()

	type Example struct {
		Val int
	}

	var ex *Example
	_ = ex.Val // this will segfault
}
