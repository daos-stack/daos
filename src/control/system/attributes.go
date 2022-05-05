//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"github.com/pkg/errors"
)

type (
	// SysAttrSetter defines an interface to be implemented by
	// something that can set system properties.
	SysAttrSetter interface {
		SetSystemAttrs(props map[string]string) error
	}
	// SysAttrGetter defines an interface to be implemented by
	// something that can get system properties.
	SysAttrGetter interface {
		GetSystemAttrs(keys []string) (map[string]string, error)
	}
)

// SetAttributes updates the system attributes with the supplied map.
// To delete an attribute, set the value to an empty string.
func SetAttributes(db SysAttrSetter, attrs map[string]string) error {
	for k := range attrs {
		if isReservedKey(k) {
			return errors.Errorf("cannot set reserved key %q", k)
		}
	}

	return db.SetSystemAttrs(attrs)
}

// getAttributes returns the system attributes for the supplied keys.
func getAttributes(db SysAttrGetter, keys []string, toUser bool) (map[string]string, error) {
	attrs, err := db.GetSystemAttrs(keys)
	if err != nil {
		return nil, err
	}

	if toUser {
		for k := range attrs {
			if isReservedKey(k) {
				delete(attrs, k)
			}
		}
	}

	return attrs, nil
}

// GetAttributes returns the user-viewable system attributes for the supplied keys.
func GetAttributes(db SysAttrGetter, keys []string) (map[string]string, error) {
	return getAttributes(db, keys, true)
}
