//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"
	"reflect"
)

// AccessControlList is a structure for the access control list.
type AccessControlList struct {
	Entries    []string // Access Control Entries in short string format
	Owner      string   // User that owns the resource
	OwnerGroup string   // Group that owns the resource
}

// Empty checks whether there are any entries in the AccessControlList
func (acl *AccessControlList) Empty() bool {
	if acl == nil || len(acl.Entries) == 0 {
		return true
	}
	return false
}

// HasOwner checks whether the AccessControlList has an owner user.
func (acl *AccessControlList) HasOwner() bool {
	if acl == nil {
		return false
	}

	if acl.Owner != "" {
		return true
	}
	return false
}

// HasOwnerGroup checks whether the AccessControlList has an owner group.
func (acl *AccessControlList) HasOwnerGroup() bool {
	if acl == nil {
		return false
	}

	if acl.OwnerGroup != "" {
		return true
	}
	return false
}

// String displays the AccessControlList in a basic string format.
func (acl *AccessControlList) String() string {
	if acl == nil {
		return "nil"
	}
	return fmt.Sprintf("%+v", *acl)
}

// PoolDiscovery represents the basic discovery information for a pool.
type PoolDiscovery struct {
	UUID        string   `json:"uuid"`     // Unique identifier
	SvcReplicas []uint32 `json:"svc_reps"` // Ranks of pool service replicas
}

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
