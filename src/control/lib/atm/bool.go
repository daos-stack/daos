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

// Package atm provides a collection of thread-safe types.
package atm

import "sync/atomic"

// Bool provides an atomic boolean value.
type Bool uint32

// NewBool returns a Bool set to the provided starting value.
func NewBool(in bool) Bool {
	var b Bool
	if in {
		b.SetTrue()
	}
	return b
}

// SetTrue sets the Bool to true.
func (b *Bool) SetTrue() {
	atomic.StoreUint32((*uint32)(b), 1)
}

// IsTrue returns true if the value is true.
func (b *Bool) IsTrue() bool {
	return b.Load()
}

// SetFalse sets the Bool to false.
func (b *Bool) SetFalse() {
	atomic.StoreUint32((*uint32)(b), 0)
}

// IsFalse returns false if the value is false.
func (b *Bool) IsFalse() bool {
	return !b.Load()
}

// Load returns a bool representing the value.
func (b *Bool) Load() bool {
	return atomic.LoadUint32((*uint32)(b)) != 0
}
