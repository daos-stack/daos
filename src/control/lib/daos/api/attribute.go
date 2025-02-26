//
// (C) Copyright 2018-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <stdint.h>
#include <uuid/uuid.h>

#include <daos_task.h>
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

func listDaosAttributes(hdl C.daos_handle_t, at attrType) ([]string, error) {
	var rc C.int
	expectedSize, totalSize := C.size_t(0), C.size_t(0)

	switch at {
	case poolAttr:
		rc = daos_pool_list_attr(hdl, nil, &totalSize, nil)
	case contAttr:
		rc = daos_cont_list_attr(hdl, nil, &totalSize, nil)
	default:
		return nil, errors.Wrapf(daos.InvalidInput, "unknown attr type %d", at)
	}
	if err := daosError(rc); err != nil {
		return nil, errors.Wrapf(err, "failed to list %s attributes", at)
	}

	if totalSize < 1 {
		return nil, nil
	}

	attrNames := []string{}
	expectedSize = totalSize
	cNamesBuf := C.malloc(totalSize)
	defer C.free(cNamesBuf)

	switch at {
	case poolAttr:
		rc = daos_pool_list_attr(hdl, (*C.char)(cNamesBuf), &totalSize, nil)
	case contAttr:
		rc = daos_cont_list_attr(hdl, (*C.char)(cNamesBuf), &totalSize, nil)
	default:
		return nil, errors.Wrapf(daos.InvalidInput, "unknown attr type %d", at)
	}
	if err := daosError(rc); err != nil {
		return nil, errors.Wrapf(err, "failed to list %s attributes", at)
	}

	if err := iterStringsBuf(cNamesBuf, expectedSize, func(name string) {
		attrNames = append(attrNames, name)
	}); err != nil {
		return nil, err
	}

	return attrNames, nil
}

// getDaosAttributes fetches the values for the given list of attribute names.
// Uses the bulk attribute fetch API to minimize roundtrips.
func getDaosAttributes(hdl C.daos_handle_t, at attrType, reqAttrNames []string) (daos.AttributeList, error) {
	if len(reqAttrNames) == 0 {
		attrNameList, err := listDaosAttributes(hdl, at)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to list %s attributes", at)
		}
		reqAttrNames = attrNameList
	}
	numAttr := len(reqAttrNames)

	if numAttr == 0 {
		return nil, nil
	}

	// First, build a slice of C strings for the requested attribute names.
	cAttrNames := make([]*C.char, numAttr)
	for i, name := range reqAttrNames {
		if name == "" {
			return nil, errors.Wrapf(daos.InvalidInput, "empty %s attribute name at index %d", at, i)
		}
		cAttrNames[i] = C.CString(name)
	}
	defer func(nameSlice []*C.char) {
		for _, name := range nameSlice {
			freeString(name)
		}
	}(cAttrNames)

	// Next, create a slice of C.size_t entries to hold the sizes of the values.
	// We have to do this first in order to know the buffer sizes to allocate
	// before fetching the actual values.
	cAttrSizes := make([]C.size_t, numAttr)
	var rc C.int
	switch at {
	case poolAttr:
		rc = daos_pool_get_attr(hdl, C.int(numAttr), &cAttrNames[0], nil, &cAttrSizes[0], nil)
	case contAttr:
		rc = daos_cont_get_attr(hdl, C.int(numAttr), &cAttrNames[0], nil, &cAttrSizes[0], nil)
	default:
		return nil, errors.Wrapf(daos.InvalidInput, "unknown attr type %d", at)
	}
	if err := daosError(rc); err != nil {
		return nil, errors.Wrapf(err, "failed to get %s attribute sizes", at)
	}

	// Now, create a slice of buffers to hold the values.
	cAttrValues := make([]unsafe.Pointer, numAttr)
	defer func(valueSlice []unsafe.Pointer) {
		for _, value := range valueSlice {
			C.free(value)
		}
	}(cAttrValues)
	for i, size := range cAttrSizes {
		if size < 1 {
			return nil, errors.Wrapf(daos.MiscError, "failed to get %s attribute %s: size is %d", at, reqAttrNames[i], size)
		}

		cAttrValues[i] = C.malloc(size)
	}

	// Do the actual fetch of all values in one go.
	switch at {
	case poolAttr:
		rc = daos_pool_get_attr(hdl, C.int(numAttr), &cAttrNames[0], &cAttrValues[0], &cAttrSizes[0], nil)
	case contAttr:
		rc = daos_cont_get_attr(hdl, C.int(numAttr), &cAttrNames[0], &cAttrValues[0], &cAttrSizes[0], nil)
	default:
		return nil, errors.Wrapf(daos.InvalidInput, "unknown attr type %d", at)
	}
	if err := daosError(rc); err != nil {
		return nil, errors.Wrapf(err, "failed to get %s attribute values", at)
	}

	// Finally, create a slice of attribute structs to hold the results.
	// Note that we are copying the values into Go-managed byte slices
	// for safety and simplicity so that we can free the C memory as soon
	// as this function exits.
	attrs := make([]*daos.Attribute, numAttr)
	for i, name := range reqAttrNames {
		attrs[i] = &daos.Attribute{
			Name:  name,
			Value: C.GoBytes(cAttrValues[i], C.int(cAttrSizes[i])),
		}
	}

	return attrs, nil
}

// setDaosAttributes sets the values for the given list of attribute names.
// Uses the bulk attribute set API to minimize roundtrips.
func setDaosAttributes(hdl C.daos_handle_t, at attrType, attrs daos.AttributeList) error {
	if len(attrs) == 0 {
		return errors.Wrapf(daos.InvalidInput, "no %s attributes provided", at)
	}

	// First, build a slice of C strings for the attribute names.
	attrNames := make([]*C.char, len(attrs))
	for i, attr := range attrs {
		if attr == nil {
			return errors.Wrapf(daos.InvalidInput, "nil %s attribute at index %d", at, i)
		}
		if attr.Name == "" {
			return errors.Wrapf(daos.InvalidInput, "empty %s attribute name at index %d", at, i)
		}
		attrNames[i] = C.CString(attr.Name)
	}
	defer func(nameSlice []*C.char) {
		for _, name := range nameSlice {
			freeString(name)
		}
	}(attrNames)

	// Next, create a slice of C.size_t entries to hold the sizes of the values,
	// and a slice of pointers to the actual values.
	attrSizes := make([]C.size_t, len(attrs))
	attrValues := make([]unsafe.Pointer, len(attrs))
	for i, attr := range attrs {
		attrSizes[i] = C.size_t(len(attr.Value))
		if attrSizes[i] == 0 {
			return errors.Wrapf(daos.InvalidInput, "empty %s attribute value at index %d", at, i)
		}
		// NB: We are copying the values into C memory for safety and simplicity.
		attrValues[i] = C.malloc(attrSizes[i])
		valSlice := unsafe.Slice((*byte)(attrValues[i]), attrSizes[i])
		copy(valSlice[:], attr.Value)
	}
	defer func(bufSlice []unsafe.Pointer) {
		for _, buf := range bufSlice {
			C.free(buf)
		}
	}(attrValues)

	attrCount := C.int(len(attrs))
	var rc C.int
	switch at {
	case poolAttr:
		rc = daos_pool_set_attr(hdl, attrCount, &attrNames[0], &attrValues[0], &attrSizes[0], nil)
	case contAttr:
		rc = daos_cont_set_attr(hdl, attrCount, &attrNames[0], &attrValues[0], &attrSizes[0], nil)
	default:
		return errors.Wrapf(daos.InvalidInput, "unknown attr type %d", at)
	}

	return errors.Wrapf(daosError(rc), "failed to set %s attributes", at)
}

// delDaosAttributes deletes the given attributes.
func delDaosAttributes(hdl C.daos_handle_t, at attrType, names []string) error {
	if len(names) == 0 {
		return errors.Wrapf(daos.InvalidInput, "no %s attribute names provided", at)
	}

	attrNames := make([]*C.char, len(names))
	for i, name := range names {
		if name == "" {
			return errors.Wrapf(daos.InvalidInput, "empty %s attribute name at index %d", at, i)
		}
		attrNames[i] = C.CString(name)
	}
	defer func(nameSlice []*C.char) {
		for _, name := range nameSlice {
			freeString(name)
		}
	}(attrNames)

	var rc C.int
	switch at {
	case poolAttr:
		rc = daos_pool_del_attr(hdl, C.int(len(attrNames)), &attrNames[0], nil)
	case contAttr:
		rc = daos_cont_del_attr(hdl, C.int(len(attrNames)), &attrNames[0], nil)
	default:
		return errors.Wrapf(daos.InvalidInput, "unknown attr type %d", at)
	}

	return errors.Wrapf(daosError(rc), "failed to delete %s attributes", at)
}
