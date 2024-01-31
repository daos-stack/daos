//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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

// NewBoolRef returns a reference to a Bool set to the
// provided starting value.
func NewBoolRef(in bool) *Bool {
	b := NewBool(in)
	return &b
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

// Store sets the Bool based on the supplied bool value.
func (b *Bool) Store(val bool) {
	if val {
		b.SetTrue()
		return
	}

	b.SetFalse()
}
