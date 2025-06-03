//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import "sort"

type (
	// Attribute is a pool or container attribute.
	Attribute struct {
		Name  string `json:"name"`
		Value []byte `json:"value,omitempty"`
	}

	// AttributeList is a list of attributes.
	AttributeList []*Attribute
)

// AsMap returns the attributes list as a map.
func (al AttributeList) AsMap() map[string][]byte {
	m := make(map[string][]byte)
	for _, a := range al {
		m[a.Name] = a.Value
	}
	return m
}

// AsList returns the attributes list as a sorted list of attribute names.
func (al AttributeList) AsList() []string {
	names := make([]string, len(al))
	for i, a := range al {
		names[i] = a.Name
	}
	sort.Strings(names)
	return names
}
