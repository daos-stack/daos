//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"reflect"
)

// InterfaceIsNil returns true if the interface itself or its underlying value
// is nil.
func InterfaceIsNil(i interface{}) bool {
	// NB: The unsafe version of this, which makes assumptions
	// about the memory layout of interfaces, is a constant
	// 2.54 ns/op.
	// return (*[2]uintptr)(unsafe.Pointer(&i))[1] == 0

	// This version is only slightly slower in the grand scheme
	// of things. 3.96 ns/op for a nil type/value, and 7.98 ns/op
	// for a non-nil value.
	if i == nil {
		return true
	}
	return reflect.ValueOf(i).IsNil()
}

// NormalExit indicates that the process exited without error.
const NormalExit ExitStatus = "process exited with 0"

// ExitStatus implements the error interface and is used to indicate external
// process exit conditions.
type ExitStatus string

func (es ExitStatus) Error() string {
	return string(es)
}

// GetExitStatus ensures that a monitored process always returns an error of
// some sort when it exits so that we can respond appropriately.
func GetExitStatus(err error) error {
	if err != nil {
		return err
	}

	return NormalExit
}
