//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui

import (
	"fmt"
	"sort"
	"strings"

	"github.com/google/uuid"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/drpc"
)

var (
	_ flags.Unmarshaler = &LabelOrUUIDFlag{}
)

// LabelOrUUIDFlag is used to hold a pool or container ID supplied
// via command-line-argument.
type LabelOrUUIDFlag struct {
	UUID  uuid.UUID `json:"uuid"`
	Label string    `json:"label"`
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

// SetLabel validates the supplied label and sets it if valid.
func (f *LabelOrUUIDFlag) SetLabel(l string) error {
	if !drpc.LabelIsValid(l) {
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

const (
	// FIXME: Pin these to something in the engine code
	maxKeyLen   = 20
	maxValueLen = 128
)

func propError(fs string, args ...interface{}) *flags.Error {
	return &flags.Error{
		Message: fmt.Sprintf(fs, args...),
	}
}

type keyMap map[string]struct{}

func (m keyMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	return keys
}

type CompletionMap map[string][]string

type SetPropertiesFlag struct {
	ParsedProps  map[string]string
	settableKeys keyMap
	completions  CompletionMap
}

func (f *SetPropertiesFlag) SettableKeys(keys ...string) {
	f.settableKeys = make(keyMap)
	for _, key := range keys {
		f.settableKeys[key] = struct{}{}
	}
}

func (f *SetPropertiesFlag) SetCompletions(comps CompletionMap) {
	f.completions = comps
}

func (f *SetPropertiesFlag) IsSettable(key string) bool {
	// If the list of settable keys is not set, default
	// to allowing all.
	if len(f.settableKeys) == 0 {
		return true
	}

	_, isSettable := f.settableKeys[key]
	return isSettable
}

func (f *SetPropertiesFlag) UnmarshalFlag(fv string) error {
	f.ParsedProps = make(map[string]string)

	for _, propStr := range strings.Split(fv, ",") {
		keyVal := strings.Split(propStr, ":")
		if len(keyVal) != 2 {
			return propError("invalid property %q (must be key:val)", propStr)
		}

		key := strings.TrimSpace(keyVal[0])
		value := strings.TrimSpace(keyVal[1])
		if !f.IsSettable(key) {
			return propError("%q is not a settable property (valid: %s)", key, strings.Join(f.settableKeys.Keys(), ","))
		}
		if len(key) == 0 {
			return propError("key must not be empty")
		}
		if len(key) > maxKeyLen {
			return propError("key too long (%d > %d)", len(key), maxKeyLen)
		}
		if len(value) == 0 {
			return propError("value must not be empty")
		}
		if len(value) > maxValueLen {
			return propError("value too long (%d > %d)", len(value), maxValueLen)
		}

		f.ParsedProps[key] = value
	}

	return nil
}

func (f *SetPropertiesFlag) Complete(match string) (comps []flags.Completion) {
	var prefix string
	propPairs := strings.Split(match, ",")
	if len(propPairs) > 1 {
		match = propPairs[len(propPairs)-1:][0]
		prefix = strings.Join(propPairs[0:len(propPairs)-1], ",")
		prefix += ","
	}

	for propKey, vals := range f.completions {
		if len(vals) == 0 || !strings.Contains(match, ":") {
			if strings.HasPrefix(propKey, match) {
				comps = append(comps, flags.Completion{Item: prefix + propKey + ":"})
			}
			continue
		}

		for _, valName := range vals {
			propVal := propKey + ":" + valName
			if strings.HasPrefix(propVal, match) {
				comps = append(comps, flags.Completion{Item: valName})
			}
		}
	}

	return
}

type GetPropertiesFlag struct {
	ParsedProps  map[string]struct{}
	gettableKeys keyMap
	completions  CompletionMap
}

func (f *GetPropertiesFlag) GettableKeys(keys ...string) {
	f.gettableKeys = make(keyMap)
	for _, key := range keys {
		f.gettableKeys[key] = struct{}{}
	}
}

func (f *GetPropertiesFlag) SetCompletions(comps CompletionMap) {
	f.completions = comps
}

func (f *GetPropertiesFlag) IsGettable(key string) bool {
	// If the list of gettable keys is not set, default
	// to allowing all.
	if len(f.gettableKeys) == 0 {
		return true
	}

	_, isGettable := f.gettableKeys[key]
	return isGettable
}

func (f *GetPropertiesFlag) UnmarshalFlag(fv string) error {
	f.ParsedProps = make(map[string]struct{})

	for _, key := range strings.Split(fv, ",") {
		key = strings.TrimSpace(key)
		if !f.IsGettable(key) {
			return propError("%q is not a gettable property (valid: %s)", key, strings.Join(f.gettableKeys.Keys(), ","))
		}
		if len(key) == 0 {
			return propError("key must not be empty")
		}
		if len(key) > maxKeyLen {
			return propError("key too long (%d > %d)", len(key), maxKeyLen)
		}

		f.ParsedProps[key] = struct{}{}
	}

	return nil
}

func (f *GetPropertiesFlag) Complete(match string) (comps []flags.Completion) {
	var prefix string
	propPairs := strings.Split(match, ",")
	if len(propPairs) > 1 {
		match = propPairs[len(propPairs)-1:][0]
		prefix = strings.Join(propPairs[0:len(propPairs)-1], ",")
		prefix += ","
	}

	for propKey := range f.completions {
		if strings.HasPrefix(propKey, match) {
			comps = append(comps, flags.Completion{Item: prefix + propKey})
		}
		continue
	}

	return
}
