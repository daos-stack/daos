//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import "testing"

// The actual test functions are in property_ctest.go file so that they can use cgo (import "C").
// These wrappers are here for gotest to find.

func TestProperty_EcCellSize(testRunner *testing.T) {
	testProperty_EcCellSize(testRunner)
}

func TestProperty_EcCellSize_Errors(testRunner *testing.T) {
	testProperty_EcCellSize_Errors(testRunner)
}
