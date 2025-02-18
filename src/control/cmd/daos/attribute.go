//
// (C) Copyright 2018-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"strings"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/daos/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
#include "util.h"
*/
import "C"

type attrType int

const (
	poolAttr attrType = iota
	contAttr
)

func (at attrType) String() string {
	switch at {
	case poolAttr:
		return "pool"
	case contAttr:
		return "container"
	default:
		return "unknown"
	}
}

type (
	attrCmd interface {
		MustLogCtx() context.Context
		cmdutil.JSONOutputter
		logging.Logger
	}

	attrListerGetter interface {
		ListAttributes(context.Context) ([]string, error)
		GetAttributes(context.Context, ...string) (daos.AttributeList, error)
	}

	attrSetter interface {
		SetAttributes(context.Context, ...*daos.Attribute) error
	}

	attrDeleter interface {
		DeleteAttributes(context.Context, ...string) error
	}
)

func listAttributes(cmd attrCmd, alg attrListerGetter, at attrType, id string, verbose bool) error {
	var attrs daos.AttributeList
	if !verbose {
		attrNames, err := alg.ListAttributes(cmd.MustLogCtx())
		if err != nil {
			return errors.Wrapf(err, "failed to list attributes for %s %s", at, id)
		}
		attrs = attrListFromNames(attrNames)
	} else {
		var err error
		attrs, err = alg.GetAttributes(cmd.MustLogCtx())
		if err != nil {
			return errors.Wrapf(err, "failed to get attributes for %s %s", at, id)
		}
	}

	if cmd.JSONOutputEnabled() {
		if verbose {
			return cmd.OutputJSON(attrs.AsMap(), nil)
		}
		return cmd.OutputJSON(attrs.AsList(), nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for %s %s:", at, id)
	pretty.PrintAttributes(&bld, title, attrs...)

	cmd.Info(bld.String())

	return nil
}

func getAttributes(cmd attrCmd, alg attrListerGetter, at attrType, id string, names ...string) error {
	attrs, err := alg.GetAttributes(cmd.MustLogCtx(), names...)
	if err != nil {
		return errors.Wrapf(err, "failed to get attributes for %s %s", at, id)
	}

	if cmd.JSONOutputEnabled() {
		// Maintain compatibility with older behavior.
		if len(names) == 1 && len(attrs) == 1 {
			return cmd.OutputJSON(attrs[0], nil)
		}
		return cmd.OutputJSON(attrs, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for %s %s:", at, id)
	pretty.PrintAttributes(&bld, title, attrs...)

	cmd.Info(bld.String())

	return nil
}

func setAttributes(cmd attrCmd, as attrSetter, at attrType, id string, attrMap map[string]string) error {
	if len(attrMap) == 0 {
		return errors.New("attribute name and value are required")
	}

	attrs := make(daos.AttributeList, 0, len(attrMap))
	for key, val := range attrMap {
		attrs = append(attrs, &daos.Attribute{
			Name:  key,
			Value: []byte(val),
		})
	}

	if err := as.SetAttributes(cmd.MustLogCtx(), attrs...); err != nil {
		return errors.Wrapf(err, "failed to set attributes on %s %s", at, id)
	}
	cmd.Infof("Attributes successfully set on %s %q", at, id)

	return nil
}

func delAttributes(cmd attrCmd, ad attrDeleter, at attrType, id string, names ...string) error {
	attrsString := strings.Join(names, ",")
	if err := ad.DeleteAttributes(cmd.MustLogCtx(), names...); err != nil {
		return errors.Wrapf(err, "failed to delete attributes %s on %s %s", attrsString, at, id)
	}
	cmd.Infof("Attribute(s) %s successfully deleted on %s %q", attrsString, at, id)

	return nil
}

// NB: These will be removed in the next patch, which adds the container APIs.
func listDaosAttributes(hdl C.daos_handle_t, at attrType, verbose bool) (daos.AttributeList, error) {
	var rc C.int
	expectedSize, totalSize := C.size_t(0), C.size_t(0)

	switch at {
	case contAttr:
		rc = C.daos_cont_list_attr(hdl, nil, &totalSize, nil)
	default:
		return nil, errors.Errorf("unknown attr type %d", at)
	}
	if err := daosError(rc); err != nil {
		return nil, err
	}

	if totalSize < 1 {
		return nil, nil
	}

	attrNames := []string{}
	expectedSize = totalSize
	buf := C.malloc(totalSize)
	defer C.free(buf)

	switch at {
	case contAttr:
		rc = C.daos_cont_list_attr(hdl, (*C.char)(buf), &totalSize, nil)
	default:
		return nil, errors.Errorf("unknown attr type %d", at)
	}
	if err := daosError(rc); err != nil {
		return nil, err
	}

	if err := iterStringsBuf(buf, expectedSize, func(name string) {
		attrNames = append(attrNames, name)
	}); err != nil {
		return nil, err
	}

	if verbose {
		return getDaosAttributes(hdl, at, attrNames)
	}

	attrs := make(daos.AttributeList, len(attrNames))
	for i, name := range attrNames {
		attrs[i] = &daos.Attribute{Name: name}
	}

	return attrs, nil

}

// getDaosAttributes fetches the values for the given list of attribute names.
// Uses the bulk attribute fetch API to minimize roundtrips.
func getDaosAttributes(hdl C.daos_handle_t, at attrType, names []string) (daos.AttributeList, error) {
	if len(names) == 0 {
		attrList, err := listDaosAttributes(hdl, at, false)
		if err != nil {
			return nil, errors.Wrap(err, "failed to list attributes")
		}
		names = make([]string, len(attrList))
		for i, attr := range attrList {
			names[i] = attr.Name
		}
	}
	numAttr := len(names)

	// First, build a slice of C strings for the attribute names.
	attrNames := make([]*C.char, numAttr)
	for i, name := range names {
		attrNames[i] = C.CString(name)
	}
	defer func(nameSlice []*C.char) {
		for _, name := range nameSlice {
			freeString(name)
		}
	}(attrNames)

	// Next, create a slice of C.size_t entries to hold the sizes of the values.
	// We have to do this first in order to know the buffer sizes to allocate
	// before fetching the actual values.
	attrSizes := make([]C.size_t, numAttr)
	var rc C.int
	switch at {
	case contAttr:
		rc = C.daos_cont_get_attr(hdl, C.int(numAttr), &attrNames[0], nil, &attrSizes[0], nil)
	default:
		return nil, errors.Errorf("unknown attr type %d", at)
	}
	if err := daosError(rc); err != nil {
		return nil, errors.Wrapf(err, "failed to get attribute sizes: %s...", names[0])
	}

	// Now, create a slice of buffers to hold the values.
	attrValues := make([]unsafe.Pointer, numAttr)
	defer func(valueSlice []unsafe.Pointer) {
		for _, value := range valueSlice {
			C.free(value)
		}
	}(attrValues)
	for i, size := range attrSizes {
		if size < 1 {
			return nil, errors.Errorf("failed to get attribute %s: size is %d", names[i], size)
		}

		attrValues[i] = C.malloc(size)
	}

	// Do the actual fetch of all values in one go.
	switch at {
	case contAttr:
		rc = C.daos_cont_get_attr(hdl, C.int(numAttr), &attrNames[0], &attrValues[0], &attrSizes[0], nil)
	default:
		return nil, errors.Errorf("unknown attr type %d", at)
	}
	if err := daosError(rc); err != nil {
		return nil, errors.Wrapf(err, "failed to get attribute values: %s...", names[0])
	}

	// Finally, create a slice of attribute structs to hold the results.
	// Note that we are copying the values into Go-managed byte slices
	// for safety and simplicity so that we can free the C memory as soon
	// as this function exits.
	attrs := make(daos.AttributeList, numAttr)
	for i, name := range names {
		attrs[i] = &daos.Attribute{
			Name:  name,
			Value: C.GoBytes(attrValues[i], C.int(attrSizes[i])),
		}
	}

	return attrs, nil
}

// getDaosAttribute fetches the value for the given attribute name.
// NB: For operations involving multiple attributes, the getDaosAttributes()
// function is preferred for efficiency.
func getDaosAttribute(hdl C.daos_handle_t, at attrType, name string) (*daos.Attribute, error) {
	attrs, err := getDaosAttributes(hdl, at, []string{name})
	if err != nil {
		return nil, err
	}
	if len(attrs) == 0 {
		return nil, errors.Errorf("attribute %q not found", name)
	}
	return attrs[0], nil
}

// setDaosAttributes sets the values for the given list of attribute names.
// Uses the bulk attribute set API to minimize roundtrips.
func setDaosAttributes(hdl C.daos_handle_t, at attrType, attrs daos.AttributeList) error {
	if len(attrs) == 0 {
		return nil
	}

	// First, build a slice of C strings for the attribute names.
	attrNames := make([]*C.char, len(attrs))
	for i, attr := range attrs {
		attrNames[i] = C.CString(attr.Name)
	}
	defer func(nameSlice []*C.char) {
		for _, name := range nameSlice {
			freeString(name)
		}
	}(attrNames)

	// Next, create a slice of C.size_t entries to hold the sizes of the values,
	// and a slice of pointers to the actual values.
	valSizes := make([]C.size_t, len(attrs))
	valBufs := make([]unsafe.Pointer, len(attrs))
	for i, attr := range attrs {
		valSizes[i] = C.size_t(len(attr.Value))
		// NB: We are copying the values into C memory for safety and simplicity.
		valBufs[i] = C.malloc(valSizes[i])
		valSlice := (*[1 << 30]byte)(valBufs[i])
		copy(valSlice[:], attr.Value)
	}
	defer func(bufSlice []unsafe.Pointer) {
		for _, buf := range bufSlice {
			C.free(buf)
		}
	}(valBufs)

	attrCount := C.int(len(attrs))
	var rc C.int
	switch at {
	case contAttr:
		rc = C.daos_cont_set_attr(hdl, attrCount, &attrNames[0], &valBufs[0], &valSizes[0], nil)
	default:
		return errors.Errorf("unknown attr type %d", at)
	}

	return daosError(rc)
}

// setDaosAttribute sets the value for the given attribute name.
// NB: For operations involving multiple attributes, the setDaosAttributes()
// function is preferred for efficiency.
func setDaosAttribute(hdl C.daos_handle_t, at attrType, attr *daos.Attribute) error {
	if attr == nil {
		return errors.Errorf("nil %T", attr)
	}

	return setDaosAttributes(hdl, at, daos.AttributeList{attr})
}

func delDaosAttribute(hdl C.daos_handle_t, at attrType, name string) error {
	attrName := C.CString(name)
	defer freeString(attrName)

	var rc C.int
	switch at {
	case contAttr:
		rc = C.daos_cont_del_attr(hdl, 1, &attrName, nil)
	default:
		return errors.Errorf("unknown attr type %d", at)
	}

	return daosError(rc)
}
