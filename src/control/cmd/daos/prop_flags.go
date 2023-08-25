//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"io"
	"strings"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
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

	props daosAPI.ContainerPropertySet
}

func (f *PropertiesFlag) Complete(match string) []flags.Completion {
	comps := make(ui.CompletionMap)
	for key, hdlr := range daosAPI.ContainerProperties {
		if !f.IsSettable(key) {
			continue
		}
		comps[key] = hdlr.ValueKeys()
	}
	f.SetCompletions(comps)

	return f.SetPropertiesFlag.Complete(match)
}

func (f *PropertiesFlag) AddPropVal(key, val string) error {
	if f.props == nil {
		f.props = make(daosAPI.ContainerPropertySet)
	}
	if err := f.props.AddValue(key, val); err != nil {
		return err
	}

	if f.ParsedProps == nil {
		f.ParsedProps = make(map[string]string)
	}
	f.ParsedProps[key] = val

	return nil
}

func (f *PropertiesFlag) UnmarshalFlag(fv string) error {
	if err := f.SetPropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	if f.props == nil {
		f.props = make(daosAPI.ContainerPropertySet)
	}
	for key, val := range f.ParsedProps {
		if err := f.props.AddValue(key, val); err != nil {
			return err
		}
	}

	return nil
}

// CreatePropertiesFlag embeds the base PropertiesFlag struct to
// compose a flag that is used for setting properties on a
// new container. It is intended to be used where only a subset of
// properties are valid for setting on create.
type CreatePropertiesFlag struct {
	PropertiesFlag
}

func (f *CreatePropertiesFlag) setWritableKeys() {
	keys := make([]string, 0, len(daosAPI.ContainerProperties))
	for key, hdlr := range daosAPI.ContainerProperties {
		if !hdlr.IsReadOnly() {
			keys = append(keys, key)
		}
	}
	f.SettableKeys(keys...)
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
	if fv == "" || len(f.names) == 0 || f.names[0] == "all" {
		f.names = daosAPI.ContainerProperties.Keys()
	}

	maxNameLen := daos.MaxPropertyNameLength
	for i, name := range f.names {
		key := strings.TrimSpace(name)
		if len(key) == 0 {
			return errors.New("name must not be empty")
		}
		if len(key) > maxNameLen {
			return errors.Errorf("%q: name too long (%d > %d)",
				key, len(key), maxNameLen)
		}
		f.names[i] = key
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

	for propKey := range daosAPI.ContainerProperties {
		if !f.IsSettable(propKey) {
			continue
		}

		if strings.HasPrefix(propKey, match) {
			comps = append(comps, flags.Completion{Item: prefix + propKey})
		}
	}

	return
}

func printProperties(out io.Writer, header string, props daosAPI.ContainerPropertySet) {
	fmt.Fprintf(out, "%s\n", header)

	if len(props) == 0 {
		fmt.Fprintln(out, "  No properties found.")
		return
	}

	nameTitle := "Name"
	valueTitle := "Value"
	titles := []string{nameTitle}

	table := []txtfmt.TableRow{}
	for _, prop := range props {
		row := txtfmt.TableRow{}
		row[nameTitle] = fmt.Sprintf("%s (%s)", prop.Description, prop.Name)
		if prop.String() != "" {
			row[valueTitle] = prop.String()
			if len(titles) == 1 {
				titles = append(titles, valueTitle)
			}
		}
		table = append(table, row)
	}

	tf := txtfmt.NewTableFormatter(titles...)
	tf.InitWriter(out)
	tf.Format(table)
}
