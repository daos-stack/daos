//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import "strings"

const (
	// userPropPrefix is the prefix for user-visible properties.
	userPropPrefix = "prop."
	// mgmtPropPrefix is the prefix for MS properties.
	mgmtPropPrefix = "mgmt."
)

func isReservedKey(key string) bool {
	return strings.HasPrefix(key, userPropPrefix) || strings.HasPrefix(key, mgmtPropPrefix)
}

// SetMgmtProperty updates the MS property for the supplied key/value.
func SetMgmtProperty(db SysAttrSetter, key, value string) error {
	key = mgmtPropPrefix + key
	return db.SetSystemAttrs(map[string]string{key: value})
}

// GetMgmtProperty returns the MS property for the supplied key.
func GetMgmtProperty(db SysAttrGetter, key string) (string, error) {
	key = mgmtPropPrefix + key
	props, err := getAttributes(db, []string{key}, false)
	if err != nil {
		return "", err
	}

	return props[key], nil
}

// DelMgmtProperty deletes the MS property for the supplied key.
func DelMgmtProperty(db SysAttrSetter, key string) error {
	return SetMgmtProperty(db, key, "")
}
