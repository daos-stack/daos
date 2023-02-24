//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui

import (
	"fmt"
	"sort"
	"strings"

	"github.com/jessevdk/go-flags"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

var (
	_ flags.Unmarshaler = &SetPropertiesFlag{}
	_ flags.Completer   = &SetPropertiesFlag{}
	_ flags.Unmarshaler = &GetPropertiesFlag{}
	_ flags.Completer   = &GetPropertiesFlag{}
)

const (
	// MaxPropKeyLen is the maximum length of a property key.
	MaxPropKeyLen = daos.MaxAttributeNameLength

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

type PropertiesFlag struct {
	completions      CompletionMap
	deprecatedKeyMap map[string]string
}

// SettableDeprecated Keys accepts a list of deprecated property keys that are settable.
func (f *PropertiesFlag) DeprecatedKeyMap(deprKeyMap map[string]string) {
	f.deprecatedKeyMap = deprKeyMap
}

// SetPropertiesFlag is used to hold a list of properties to set.
type SetPropertiesFlag struct {
	PropertiesFlag
	ParsedProps  map[string]string
	settableKeys common.StringSet
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

	if _, isSettable := f.settableKeys[key]; isSettable {
		return true
	}

	if len(f.deprecatedKeyMap) == 0 {
		return false
	}

	_, isSettable := f.deprecatedKeyMap[key]
	return isSettable
}

// splitFn generates a function suitable for use with
// strings.FieldsFunc that will split on the specified
// separator, but ignore the separator if it is inside
// quotes or is escaped. NB: Quotes must be balanced!
func splitFn(sep rune, max int) func(r rune) bool {
	var count int
	var inDq bool
	var inSq bool
	var inEsc bool
	return func(r rune) bool {
		if !inSq && r == '"' {
			inDq = !inDq
		}
		if !inDq && r == '\'' {
			inSq = !inSq
		}
		if r == '\\' {
			inEsc = true
			return false
		}
		if max > 0 && count >= max {
			return false
		}
		if !inDq && !inSq && !inEsc && r == sep {
			count++
			return true
		}
		inEsc = false
		return false
	}
}

// UnmarshalFlag implements the go-flags.Unmarshaler interface and is
// used to parse the user-supplied properties.
func (f *SetPropertiesFlag) UnmarshalFlag(fv string) error {
	f.ParsedProps = make(map[string]string)

	if fv == "" {
		return propError("empty property")
	}

	// Split on comma, but ignore commas inside quotes.
	for _, propStr := range strings.FieldsFunc(fv, splitFn(rune(propSep[0]), -1)) {
		// Split on colon, but ignore colons inside quotes. Result is
		// like SplitN, but with quotes ignored.
		keyVal := strings.FieldsFunc(propStr, splitFn(rune(propKvSep[0]), 1))
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
			return propError("%q: key too long (%d > %d)", key, len(key), MaxPropKeyLen)
		}
		if newKey, found := f.deprecatedKeyMap[key]; found {
			key = newKey
		}
		if len(value) == 0 {
			return propError("value must not be empty")
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
	PropertiesFlag
	ParsedProps  common.StringSet
	gettableKeys common.StringSet
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

	if _, isGettable := f.gettableKeys[key]; isGettable {
		return true
	}

	if len(f.deprecatedKeyMap) == 0 {
		return false
	}

	_, isGettable := f.deprecatedKeyMap[key]
	return isGettable
}

// UnmarshalFlag implements the go-flags.Unmarshaler interface and is
// used to parse the user-supplied list of properties to get.
func (f *GetPropertiesFlag) UnmarshalFlag(fv string) error {
	f.ParsedProps = make(common.StringSet)

	if fv == "" {
		return propError("key must not be empty")
	}

	// Split on comma, but ignore commas inside quotes.
	for _, key := range strings.FieldsFunc(fv, splitFn(rune(propSep[0]), -1)) {
		key = strings.TrimSpace(key)
		if !f.IsGettable(key) {
			return propError("%q is not a gettable property (valid: %s)", key, strings.Join(f.gettableKeys.ToSlice(), ","))
		}
		if len(key) == 0 {
			return propError("key must not be empty")
		}
		if len(key) > MaxPropKeyLen {
			return propError("%q: key too long (%d > %d)", key, len(key), MaxPropKeyLen)
		}
		// Check for unescaped : in key.
		if len(strings.FieldsFunc(key, splitFn(rune(propKvSep[0]), -1))) > 1 {
			return propError("%q: key cannot contain '"+propKvSep+"'", key)
		}
		if newKey, found := f.deprecatedKeyMap[key]; found {
			key = newKey
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
