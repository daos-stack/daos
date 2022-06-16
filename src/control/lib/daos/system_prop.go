//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package daos

import (
	"encoding/json"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
)

// BoolPropVal is a boolean property value.
type BoolPropVal bool

// NewBoolPropVal returns a new BoolPropVal initialized to a default value.
func NewBoolPropVal(defVal bool) *BoolPropVal {
	pv := BoolPropVal(defVal)
	return &pv
}

func (pv *BoolPropVal) Handler(val string) error {
	if pv == nil {
		return errors.Errorf("%T is nil", pv)
	}

	switch strings.TrimSpace(strings.ToLower(val)) {
	case "true", "1", "yes", "on":
		*pv = true
	case "false", "0", "no", "off":
		*pv = false
	default:
		return errors.Errorf("invalid value %q (valid: %s)", val, strings.Join(pv.Choices(), ","))
	}

	return nil
}

func (pv *BoolPropVal) String() string {
	if pv == nil {
		return "(nil)"
	}

	if *pv {
		return "true"
	}
	return "false"
}

func (pv BoolPropVal) Choices() []string {
	return []string{"true", "false"}
}

func (pv BoolPropVal) copy() SystemPropertyValue {
	return NewBoolPropVal(bool(pv))
}

// StringPropVal is a string-based property value.
type StringPropVal struct {
	value        string
	valueChoices []string
}

// NewStringPropVal returns a new StringPropVal initialized to
// a default value, with an optional set of allowable values.
func NewStringPropVal(defVal string, choices ...string) *StringPropVal {
	return &StringPropVal{
		value:        defVal,
		valueChoices: choices,
	}
}

func (pv *StringPropVal) Handler(val string) error {
	if pv == nil {
		return errors.Errorf("%T is nil", pv)
	}

	if len(pv.valueChoices) == 0 {
		pv.value = val
		return nil
	}

	for _, choice := range pv.valueChoices {
		if choice == val {
			pv.value = val
			return nil
		}
	}

	return errors.Errorf("invalid value %q (valid: %s)", val, strings.Join(pv.valueChoices, ","))
}

func (pv *StringPropVal) String() string {
	if pv == nil {
		return "(nil)"
	}
	return pv.value
}

func (pv *StringPropVal) Choices() []string {
	if pv == nil {
		return nil
	}
	return pv.valueChoices
}

func (pv *StringPropVal) copy() SystemPropertyValue {
	return &StringPropVal{
		value:        pv.value,
		valueChoices: pv.valueChoices,
	}
}

// CompPropVal is a computed property value that is read-only from the
// user perspective.
type CompPropVal struct {
	ValueSource func() string
}

// NewCompPropVal returns a new CompPropVal initialized with a value source.
func NewCompPropVal(valueSource func() string) SystemPropertyValue {
	return &CompPropVal{
		ValueSource: valueSource,
	}
}

func (pv *CompPropVal) Handler(string) error {
	return errors.New("computed property values may not be set")
}

func (pv *CompPropVal) String() string {
	if pv == nil || pv.ValueSource == nil {
		return "(nil)"
	}
	return pv.ValueSource()
}

func (pv *CompPropVal) Choices() []string {
	return nil
}

func (pv *CompPropVal) copy() SystemPropertyValue {
	// This is read-only anyhow so don't waste time copying.
	return pv
}

// UnsetPropVal returns a SystemPropertyValue that
// can be used to indicate that a property is not set.
func UnsetPropVal() SystemPropertyValue {
	return &StringPropVal{}
}

// PropValIsUnset returns a bool indicating whether or not the given value is
// nil or unset.
func PropValIsUnset(val SystemPropertyValue) bool {
	if common.InterfaceIsNil(val) {
		return true
	}
	return val.String() == ""
}

// SystemPropertyValue defines an interface to be implemented by all system
// property value types.
type SystemPropertyValue interface {
	Handler(val string) error
	Choices() []string
	String() string
	copy() SystemPropertyValue
}

// SystemProperty represents a key/value pair associated with a system property.
type SystemProperty struct {
	Key         SystemPropertyKey
	Value       SystemPropertyValue
	Description string
}

func (sp *SystemProperty) String() string {
	if sp == nil {
		return "(nil)"
	}
	return sp.Key.String() + ":" + sp.Value.String()
}

func (sp *SystemProperty) MarshalJSON() ([]byte, error) {
	return json.Marshal(struct {
		Key         string `json:"key"`
		Value       string `json:"value"`
		Description string `json:"description"`
	}{
		Key:         sp.Key.String(),
		Value:       sp.Value.String(),
		Description: sp.Description,
	})
}

// SystemProperty defines a type to be used to represent DAOS system property keys.
type SystemPropertyKey int

// IsValid returns a boolean indicating whether or not the system property key
// is valid.
func (sp SystemPropertyKey) IsValid() bool {
	return sp > systemPropertyUnknown && sp < systemPropertyMax
}

func (sp SystemPropertyKey) String() string {
	if str, found := map[SystemPropertyKey]string{
		SystemPropertyDaosVersion: "daos_version",
		SystemPropertyDaosSystem:  "daos_system",
	}[sp]; found {
		return str
	}

	return "unknown"
}

// FromString initializes a SystemPropertyKey from a string.
func (sp *SystemPropertyKey) FromString(val string) error {
	if sp == nil {
		return errors.Errorf("%T is nil", sp)
	}
	if val == "" {
		return errors.New("empty string")
	}

	for i := systemPropertyUnknown + 1; i < systemPropertyMax; i++ {
		if strings.EqualFold(val, i.String()) {
			*sp = i
			return nil
		}
	}

	return errors.Errorf("unknown system property key %q", val)
}

// NB: System properties are not tied to engine constants, but are defined
// here for consistency.
const (
	systemPropertyUnknown SystemPropertyKey = iota

	// SystemPropertyDaosVersion retrieves the DAOS version.
	SystemPropertyDaosVersion
	// SystemPropertyDaosSystem retrieves the DAOS system name.
	SystemPropertyDaosSystem

	// NB: This must be the last entry.
	systemPropertyMax
)

type (
	// SystemPropertyMap is a map of SystemPropertyKey to SystemProperty.
	SystemPropertyMap map[SystemPropertyKey]SystemProperty
)

// Keys returns a sorted slice of key strings.
func (spm SystemPropertyMap) Keys() []string {
	keys := common.NewStringSet()
	for key := range spm {
		keys.Add(key.String())
	}
	return keys.ToSlice()
}

// Iter returns a channel to be used to iterate over the SystemPropertyMap,
// sorted by keys.
func (spm SystemPropertyMap) Iter() <-chan *SystemProperty {
	ch := make(chan *SystemProperty)
	go func() {
		for _, strKey := range spm.Keys() {
			if prop, found := spm.Get(strKey); found {
				ch <- prop
			}
		}
		close(ch)
	}()
	return ch
}

// Get returns the SystemProperty associated with the given key, if found.
func (spm SystemPropertyMap) Get(strKey string) (*SystemProperty, bool) {
	var key SystemPropertyKey
	if err := key.FromString(strKey); err != nil {
		return nil, false
	}

	if prop, found := spm[key]; found {
		cpy := new(SystemProperty)
		*cpy = prop
		// Return a copy of the value to be used for handling input, etc.
		cpy.Value = prop.Value.copy()
		return cpy, true
	}

	return nil, false
}

// Add adds a SystemProperty to the map.
func (spm SystemPropertyMap) Add(prop SystemProperty) error {
	if spm == nil {
		return errors.Errorf("%T is nil", spm)
	}
	if _, found := spm[prop.Key]; found {
		return errors.Errorf("system property %q already exists", prop.Key)
	}

	spm[prop.Key] = prop
	return nil
}

// UpdateCompPropVal updates the value source function for a computed property value.
func (spm SystemPropertyMap) UpdateCompPropVal(key SystemPropertyKey, sourceFn func() string) error {
	if spm == nil {
		return errors.Errorf("%T is nil", spm)
	}

	if prop, found := spm[key]; found {
		if pv, ok := prop.Value.(*CompPropVal); ok {
			pv.ValueSource = sourceFn
			return nil
		}
		return errors.Errorf("system property %q is not computed", key)
	}

	return errors.Errorf("system property %q does not exist", key)
}

// SystemProperties returns the map of standard system properties.
func SystemProperties() SystemPropertyMap {
	return SystemPropertyMap{
		SystemPropertyDaosVersion: SystemProperty{
			Key:         SystemPropertyDaosVersion,
			Value:       &CompPropVal{ValueSource: func() string { return build.DaosVersion }},
			Description: "DAOS version",
		},
		SystemPropertyDaosSystem: SystemProperty{
			Key:         SystemPropertyDaosSystem,
			Value:       &CompPropVal{ValueSource: func() string { return build.DefaultSystemName }},
			Description: "DAOS system name",
		},
	}
}
