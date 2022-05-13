//
// (C) Copyright 2021-2022 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

var (
	_ flags.Unmarshaler = &LabelOrUUIDFlag{}
	_ flags.Unmarshaler = &SetPropertiesFlag{}
	_ flags.Completer   = &SetPropertiesFlag{}
	_ flags.Unmarshaler = &GetPropertiesFlag{}
	_ flags.Completer   = &GetPropertiesFlag{}
)

// LabelOrUUIDFlag is used to hold a pool or container ID supplied
// via command-line argument.
type LabelOrUUIDFlag struct {
	UUID  uuid.UUID
	Label string
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
	if !daos.LabelIsValid(l) {
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
	// MaxPropKeyLen is the maximum length of a property key.
	MaxPropKeyLen = 20
	// MaxPropValLen is the maximum length of a property value.
	MaxPropValLen = 128

	propKvSep = ":"
	propSep   = ","
)

func propError(fs string, args ...interface{}) *flags.Error {
	return &flags.Error{
		Message: fmt.Sprintf(fs, args...),
	}
}

// CompletionMap is a map of key to a list of possible completions.
type CompletionMap map[string][]string

// SetPropertiesFlag is used to hold a list of properties to set.
type SetPropertiesFlag struct {
	ParsedProps  map[string]string
	settableKeys common.StringSet
	completions  CompletionMap
}

// SettableKeys accepts a list of property keys that are settable.
func (f *SetPropertiesFlag) SettableKeys(keys ...string) {
	f.settableKeys = make(common.StringSet)
	for _, key := range keys {
		f.settableKeys.Add(key)
	}
}

// SetCompletions sets the possible completions for the SetPropertiesFlag.
func (f *SetPropertiesFlag) SetCompletions(comps CompletionMap) {
	f.completions = comps
}

// IsSettable returns true if the key is in the list of settable keys.
func (f *SetPropertiesFlag) IsSettable(key string) bool {
	// If the list of settable keys is not set, default
	// to allowing all.
	if len(f.settableKeys) == 0 {
		return true
	}

	_, isSettable := f.settableKeys[key]
	return isSettable
}

// UnmarshalFlag implements the go-flags.Unmarshaler interface and is
// used to parse the user-supplied properties.
func (f *SetPropertiesFlag) UnmarshalFlag(fv string) error {
	f.ParsedProps = make(map[string]string)

	for _, propStr := range strings.Split(fv, propSep) {
		keyVal := strings.Split(propStr, propKvSep)
		if len(keyVal) != 2 {
			return propError("invalid property %q (must be key"+propKvSep+"val)", propStr)
		}

		key := strings.TrimSpace(keyVal[0])
		value := strings.TrimSpace(keyVal[1])
		if !f.IsSettable(key) {
			return propError("%q is not a settable property (valid: %s)", key, strings.Join(f.settableKeys.ToSlice(), ","))
		}
		if len(key) == 0 {
			return propError("key must not be empty")
		}
		if len(key) > MaxPropKeyLen {
			return propError("key too long (%d > %d)", len(key), MaxPropKeyLen)
		}
		if len(value) == 0 {
			return propError("value must not be empty")
		}
		if len(value) > MaxPropValLen {
			return propError("value too long (%d > %d)", len(value), MaxPropValLen)
		}

		f.ParsedProps[key] = value
	}

	return nil
}

// Complete implements the go-flags.Completer interface and is used
// to suggest possible completions for the supplied input string.
func (f *SetPropertiesFlag) Complete(match string) (comps []flags.Completion) {
	var prefix string
	propPairs := strings.Split(match, propSep)
	// Handle key:val,key: completions.
	if len(propPairs) > 1 {
		match = propPairs[len(propPairs)-1:][0]
		prefix = strings.Join(propPairs[0:len(propPairs)-1], propSep)
		prefix += propSep
	}

	for propKey, vals := range f.completions {
		if len(vals) == 0 || !strings.Contains(match, propKvSep) {
			// If key has already been supplied, don't suggest it again.
			if strings.Contains(prefix, propKey+propKvSep) {
				continue
			}

			if strings.HasPrefix(propKey, match) {
				comps = append(comps, flags.Completion{Item: prefix + propKey + propKvSep})
			}
			continue
		}

		for _, valName := range vals {
			propVal := propKey + propKvSep + valName
			if strings.HasPrefix(propVal, match) {
				comps = append(comps, flags.Completion{Item: valName})
			}
		}
	}

	sort.Slice(comps, func(i, j int) bool {
		return comps[i].Item < comps[j].Item
	})

	return
}

// GetPropertiesFlag is used to hold a list of property keys to get.
type GetPropertiesFlag struct {
	ParsedProps  common.StringSet
	gettableKeys common.StringSet
	completions  CompletionMap
}

// GettableKeys accepts a list of property keys that are gettable.
func (f *GetPropertiesFlag) GettableKeys(keys ...string) {
	f.gettableKeys = make(common.StringSet)
	for _, key := range keys {
		f.gettableKeys.Add(key)
	}
}

// SetCompletions sets the possible completions for the GetPropertiesFlag.
func (f *GetPropertiesFlag) SetCompletions(comps CompletionMap) {
	f.completions = comps
}

// IsGettable returns true if the key is in the list of gettable keys.
func (f *GetPropertiesFlag) IsGettable(key string) bool {
	// If the list of gettable keys is not set, default
	// to allowing all.
	if len(f.gettableKeys) == 0 {
		return true
	}

	_, isGettable := f.gettableKeys[key]
	return isGettable
}

// UnmarshalFlag implements the go-flags.Unmarshaler interface and is
// used to parse the user-supplied list of properties to get.
func (f *GetPropertiesFlag) UnmarshalFlag(fv string) error {
	f.ParsedProps = make(common.StringSet)

	for _, key := range strings.Split(fv, propSep) {
		key = strings.TrimSpace(key)
		if !f.IsGettable(key) {
			return propError("%q is not a gettable property (valid: %s)", key, strings.Join(f.gettableKeys.ToSlice(), ","))
		}
		if len(key) == 0 {
			return propError("key must not be empty")
		}
		if len(key) > MaxPropKeyLen {
			return propError("key too long (%d > %d)", len(key), MaxPropKeyLen)
		}
		if strings.Contains(key, propKvSep) {
			return propError("key cannot contain '" + propKvSep + "'")
		}

		f.ParsedProps.Add(key)
	}

	return nil
}

// Complete implements the go-flags.Completer interface and is used
// to suggest possible completions for the supplied input string.
func (f *GetPropertiesFlag) Complete(match string) (comps []flags.Completion) {
	var prefix string
	propPairs := strings.Split(match, propSep)
	// Handle key,key, completion.
	if len(propPairs) > 1 {
		match = propPairs[len(propPairs)-1:][0]
		prefix = strings.Join(propPairs[0:len(propPairs)-1], propSep)
		prefix += propSep
	}

	for propKey := range f.completions {
		// If key has already been supplied, don't suggest it again.
		if strings.Contains(prefix, propKey) {
			continue
		}

		if strings.HasPrefix(propKey, match) {
			comps = append(comps, flags.Completion{Item: prefix + propKey})
		}
		continue
	}

	sort.Slice(comps, func(i, j int) bool {
		return comps[i].Item < comps[j].Item
	})

	return
}
