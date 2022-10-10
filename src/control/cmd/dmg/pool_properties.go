//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/jessevdk/go-flags"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

type PoolSetPropsFlag struct {
	ui.SetPropertiesFlag

	ToSet []*daos.PoolProperty
}

func (f *PoolSetPropsFlag) UnmarshalFlag(fv string) error {
	propHdlrs := daos.PoolProperties()
	f.SettableKeys(propHdlrs.Keys()...)
	f.DeprecatedKeyMap(daos.PoolDeprecatedProperties())

	if err := f.SetPropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	for _, key := range propHdlrs.Keys() {
		val, parsed := f.ParsedProps[key]
		if !parsed {
			continue
		}
		hdlr := propHdlrs[key]
		p := hdlr.GetProperty(key)
		if err := p.SetValue(val); err != nil {
			return err
		}
		f.ToSet = append(f.ToSet, p)
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
