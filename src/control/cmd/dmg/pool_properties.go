//
// (C) Copyright 2021-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"sort"

	"github.com/jessevdk/go-flags"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

// PoolPropSet is a non-duplicated set of pool properties.
type PoolPropSet map[string]*daos.PoolProperty

// Slice returns a sorted slice of the pool properties in the set.
func (pps PoolPropSet) Slice() []*daos.PoolProperty {
	slice := make([]*daos.PoolProperty, 0, len(pps))
	for _, prop := range pps {
		slice = append(slice, prop)
	}
	sort.Slice(slice, func(i, j int) bool {
		return slice[i].Name < slice[j].Name
	})

	return slice
}

// Add adds a property to the set, or overrides the existing value.
func (pps PoolPropSet) Add(prop *daos.PoolProperty) {
	pps[prop.Name] = prop
}

// HasProp checks if the named prop has already been added to the set.
func (pps PoolPropSet) HasProp(name string) bool {
	_, found := pps[name]
	return found
}

type PoolSetPropsFlag struct {
	ui.SetPropertiesFlag

	ToSet PoolPropSet
}

func (f *PoolSetPropsFlag) UnmarshalFlag(fv string) error {
	propHdlrs := daos.PoolProperties()
	f.SettableKeys(propHdlrs.Keys()...)
	f.DeprecatedKeyMap(daos.PoolDeprecatedProperties())

	if err := f.SetPropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	if f.ToSet == nil {
		f.ToSet = make(PoolPropSet)
	}
	for _, key := range propHdlrs.Keys() {
		val, parsed := f.ParsedProps[key]
		if !parsed {
			continue
		}
		if f.ToSet.HasProp(key) {
			return fmt.Errorf("%q: same key was specified more than once", key)
		}
		hdlr := propHdlrs[key]
		p := hdlr.GetProperty(key)
		if err := p.SetValue(val); err != nil {
			return err
		}
		f.ToSet.Add(p)
	}

	return nil
}

func (f *PoolSetPropsFlag) Complete(match string) []flags.Completion {
	comps := make(ui.CompletionMap)
	for key, hdlr := range daos.PoolProperties() {
		comps[key] = hdlr.Values()
	}
	f.SetCompletions(comps)

	return f.SetPropertiesFlag.Complete(match)
}

type PoolGetPropsFlag struct {
	ui.GetPropertiesFlag

	ToGet []*daos.PoolProperty
}

func (f *PoolGetPropsFlag) UnmarshalFlag(fv string) error {
	propHdlrs := daos.PoolProperties()
	f.GettableKeys(propHdlrs.Keys()...)
	f.DeprecatedKeyMap(daos.PoolDeprecatedProperties())

	if err := f.GetPropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	for key := range f.ParsedProps {
		hdlr := propHdlrs[key]
		f.ToGet = append(f.ToGet, hdlr.GetProperty(key))
	}

	return nil
}

func (f *PoolGetPropsFlag) Complete(match string) []flags.Completion {
	comps := make(ui.CompletionMap)
	for key, hdlr := range daos.PoolProperties() {
		comps[key] = hdlr.Values()
	}
	f.SetCompletions(comps)

	return f.GetPropertiesFlag.Complete(match)
}
