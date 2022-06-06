//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

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

// SetUserProperties sets the user-visible properties based on the supplied key/value pairs.
func SetUserProperties(db SysAttrSetter, sysProps daos.SystemPropertyMap, toSet map[string]string) error {
	propAttrs := make(map[string]string)

	for k, v := range toSet {
		prop, ok := sysProps.Get(k)
		if !ok {
			return errors.Errorf("unknown property %q", k)
		}

		if err := prop.Value.Handler(v); err != nil {
			return errors.Wrapf(err, "invalid value for property %q", k)
		}

		propAttrs[userPropPrefix+k] = prop.Value.String()
	}

	if err := db.SetSystemAttrs(propAttrs); err != nil {
		return errors.Wrap(err, "failed to set properties")
	}

	return nil
}

// GetUserProperties returns the user-visible properties for the supplied keys, or
// all user-visible properties if no keys are supplied.
func GetUserProperties(db SysAttrGetter, sysProps daos.SystemPropertyMap, keys []string) (map[string]string, error) {
	propAttrs, err := db.GetSystemAttrs(nil, func(k string) bool {
		return !strings.HasPrefix(k, userPropPrefix)
	})
	if err != nil {
		return nil, err
	}

	userProps := make(map[string]string)
	for k, v := range propAttrs {
		userProps[strings.TrimPrefix(k, userPropPrefix)] = v
	}

	if len(keys) == 0 {
		// If the request was for all system properties, then fill in any unset properties
		// with the default values.
		for prop := range sysProps.Iter() {
			if _, ok := userProps[prop.Key.String()]; !ok {
				userProps[prop.Key.String()] = prop.Value.String()
			}
		}
	} else {
		// Otherwise, fill in any unset properties with the default values.
		for _, k := range keys {
			if _, ok := userProps[k]; !ok {
				prop, ok := sysProps.Get(k)
				if !ok {
					return nil, errors.Errorf("unknown property %q", k)
				}
				userProps[k] = prop.Value.String()
			}
		}
	}

	return userProps, nil
}

// GetUserProperty returns a single user-visible property for the supplied key, or
// an error if the key is unknown.
func GetUserProperty(db SysAttrGetter, sysProps daos.SystemPropertyMap, key string) (val string, err error) {
	props, err := GetUserProperties(db, sysProps, []string{key})
	if err != nil {
		return "", err
	}

	return props[key], nil
}
