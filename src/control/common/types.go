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
	UUID        string   // Unique identifier
	SvcReplicas []uint32 `json:"Svcreps"` // Ranks of pool service replicas
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
