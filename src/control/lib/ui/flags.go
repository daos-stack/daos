//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui

import (
	"github.com/google/uuid"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
)

var (
	_ flags.Unmarshaler = &LabelOrUUIDFlag{}
)

// LabelOrUUIDFlag is used to hold a pool or container ID supplied
// via command-line-argument.
type LabelOrUUIDFlag struct {
	UUID         uuid.UUID
	Label        string
	validLabelfn func(string) bool
}

// Empty returns true if neither UUID or Label were set.
func (f LabelOrUUIDFlag) Empty() bool {
	return !f.HasLabel() && !f.HasUUID()
}

// HasLabel returns true if Label is a nonempty string.
func (f LabelOrUUIDFlag) HasLabel() bool {
	return f.Label != ""
}

// HasUUID returns true if UUID is a nonzero value.
func (f LabelOrUUIDFlag) HasUUID() bool {
	return f.UUID != uuid.Nil
}

func (f LabelOrUUIDFlag) String() string {
	switch {
	case f.HasLabel():
		return f.Label
	case f.HasUUID():
		return f.UUID.String()
	default:
		return "<no label or uuid set>"
	}
}

func (f LabelOrUUIDFlag) validLabel(l string) bool {
	if f.validLabelfn == nil {
		return l != ""
	}

	return f.validLabelfn(l)
}

// SetLabelValidator allows a custom label validation function
// to be supplied. By default, all non-empty strings are valid labels.
func (f *LabelOrUUIDFlag) SetLabelValidator(fn func(string) bool) {
	f.validLabelfn = fn
}

// SetLabel validates the supplied label and sets it if valid.
func (f *LabelOrUUIDFlag) SetLabel(l string) error {
	if !f.validLabel(l) {
		return errors.Errorf("invalid label %q", l)
	}

	f.Label = l
	return nil
}

// UnmarshalFlag implements the go-flags.Unmarshaler
// interface.
func (f *LabelOrUUIDFlag) UnmarshalFlag(fv string) error {
	uuid, err := uuid.Parse(fv)
	if err == nil {
		f.UUID = uuid
		return nil
	}

	return f.SetLabel(fv)
}
