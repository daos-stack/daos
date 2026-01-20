//
// (C) Copyright 2021-2023 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"strings"

	"github.com/jessevdk/go-flags"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

// PropertiesFlag implements the flags.Unmarshaler and flags.Completer
// interfaces in order to provide a custom flag type for converting
// command-line arguments into a *C.daos_prop_t array suitable for
// creating a container. Use the SetPropertiesFlag type for setting
// properties on an existing container and the GetPropertiesFlag type
// for getting properties on an existing container.
type PropertiesFlag struct {
	ui.SetPropertiesFlag

	propList *daos.ContainerPropertyList
}

func (f *PropertiesFlag) Complete(match string) []flags.Completion {
	comps := make(ui.CompletionMap)
	propList, err := daos.NewContainerPropertyList()
	if err != nil {
		return nil
	}
	defer propList.Free()

	for _, prop := range propList.Properties() {
		if !f.IsSettable(prop.Name) {
			continue
		}
		comps[prop.Name] = prop.SettableValues()
	}
	f.SetCompletions(comps)

	return f.SetPropertiesFlag.Complete(match)
}

func (f *PropertiesFlag) AddPropVal(key, val string) error {
	if f.propList == nil {
		var err error
		if f.propList, err = daos.AllocateContainerPropertyList(0); err != nil {
			return err
		}
	}

	prop, err := f.propList.AddEntryByName(key)
	if err != nil {
		return err
	}

	if err := prop.Set(val); err != nil {
		return err
	}

	if f.ParsedProps == nil {
		f.ParsedProps = make(map[string]string)
	}
	f.ParsedProps[key] = val

	return nil
}

func (f *PropertiesFlag) UnmarshalFlag(fv string) (err error) {
	defer func() {
		if err != nil {
			f.Cleanup()
		}
	}()

	f.propList, err = daos.AllocateContainerPropertyList(0)
	if err != nil {
		return
	}

	if err = f.SetPropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	for key, val := range f.ParsedProps {
		var prop *daos.ContainerProperty
		prop, err = f.propList.AddEntryByName(key)
		if err != nil {
			return
		}
		if err = prop.Set(val); err != nil {
			return
		}
	}

	return nil
}

// Cleanup takes care of deallocating any C resources allocated as part of
// input handling.
func (f *PropertiesFlag) Cleanup() {
	if f.propList == nil {
		return
	}

	f.propList.Free()
	f.propList = nil
}

// CreatePropertiesFlag embeds the base PropertiesFlag struct to
// compose a flag that is used for setting properties on a
// new container. It is intended to be used where only a subset of
// properties are valid for setting on create.
type CreatePropertiesFlag struct {
	PropertiesFlag
}

func (f *CreatePropertiesFlag) setWritableKeys() {
	propList, err := daos.NewContainerPropertyList()
	if err != nil {
		return
	}
	defer propList.Free()

	f.SettableKeys(propList.PropertyNames(true)...)
}

func (f *CreatePropertiesFlag) Complete(match string) []flags.Completion {
	f.setWritableKeys()

	return f.PropertiesFlag.Complete(match)
}

func (f *CreatePropertiesFlag) UnmarshalFlag(fv string) error {
	f.setWritableKeys()

	if err := f.PropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	return nil
}

// SetPropertiesFlag embeds the base PropertiesFlag struct to
// compose a flag that is used for setting properties on a
// container. It is intended to be used where only a subset of
// properties are valid for setting.
type SetPropertiesFlag struct {
	PropertiesFlag
}

func (f *SetPropertiesFlag) Complete(match string) []flags.Completion {
	f.SettableKeys("label", "status")

	return f.PropertiesFlag.Complete(match)
}

func (f *SetPropertiesFlag) UnmarshalFlag(fv string) error {
	f.SettableKeys("label", "status")

	if err := f.PropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	return nil
}

type GetPropertiesFlag struct {
	PropertiesFlag

	names []string
}

func (f *GetPropertiesFlag) UnmarshalFlag(fv string) error {
	// Accept a list of property names to fetch, if specified,
	// otherwise just fetch all known properties.
	f.names = strings.Split(fv, ",")
	if f.names[0] == "all" || f.names[0] == "" {
		f.names = nil
	}

	return nil
}

func (f *GetPropertiesFlag) Complete(match string) (comps []flags.Completion) {
	var prefix string
	propNames := strings.Split(match, ",")
	if len(propNames) > 1 {
		match = propNames[len(propNames)-1:][0]
		prefix = strings.Join(propNames[0:len(propNames)-1], ",")
		prefix += ","
	}

	propList, err := daos.NewContainerPropertyList()
	if err != nil {
		return nil
	}
	defer propList.Free()

	for _, propKey := range propList.PropertyNames(false) {
		if strings.HasPrefix(propKey, match) {
			comps = append(comps, flags.Completion{Item: prefix + propKey})
		}
	}

	return
}
