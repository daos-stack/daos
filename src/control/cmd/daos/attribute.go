//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"io"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

/*
#include "util.h"
*/
import "C"

type (
	attribute struct {
		Name  string `json:"name"`
		Value string `json:"value,omitempty"`
	}
)

func printAttributes(out io.Writer, header string, attrs ...*attribute) {
	fmt.Fprintf(out, "%s\n", header)

	if len(attrs) == 0 {
		fmt.Fprintln(out, "  No attributes found.")
		return
	}

	nameTitle := "Name"
	valueTitle := "Value"
	titles := []string{nameTitle}

	table := []txtfmt.TableRow{}
	for _, attr := range attrs {
		row := txtfmt.TableRow{}
		row[nameTitle] = attr.Name
		if attr.Value != "" {
			row[valueTitle] = attr.Value
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

type attrType int

const (
	poolAttr attrType = iota
	contAttr
)

func listDaosAttributes(hdl C.daos_handle_t, at attrType, verbose bool) ([]*attribute, error) {
	var rc C.int
	expectedSize, totalSize := C.size_t(0), C.size_t(0)

	switch at {
	case poolAttr:
		rc = C.daos_pool_list_attr(hdl, nil, &totalSize, nil)
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
	if buf == nil {
		return nil, errors.New("failed to malloc buf")
	}
	defer C.free(buf)

	switch at {
	case poolAttr:
		rc = C.daos_pool_list_attr(hdl, (*C.char)(buf), &totalSize, nil)
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

	var err error
	attrs := make([]*attribute, len(attrNames))
	for i, name := range attrNames {
		if verbose {
			attrs[i], err = getDaosAttribute(hdl, at, name)
			if err != nil {
				return nil, errors.Wrap(err, "list attributes failed")
			}
		} else {
			attrs[i] = &attribute{Name: name}
		}
	}

	return attrs, nil
}

func getDaosAttribute(hdl C.daos_handle_t, at attrType, name string) (*attribute, error) {
	var rc C.int
	var attrSize C.size_t

	attrName := C.CString(name)
	defer freeString(attrName)

	switch at {
	case poolAttr:
		rc = C.daos_pool_get_attr(hdl, 1, &attrName, nil, &attrSize, nil)
	case contAttr:
		rc = C.daos_cont_get_attr(hdl, 1, &attrName, nil, &attrSize, nil)
	default:
		return nil, errors.Errorf("unknown attr type %d", at)
	}
	if err := daosError(rc); err != nil {
		return nil, errors.Wrapf(err, "failed to get attribute %q", name)
	}

	attr := &attribute{
		Name: name,
	}

	if attrSize > 0 {
		buf := C.malloc(attrSize)
		if buf == nil {
			return nil, errors.New("failed to malloc buf")
		}
		defer C.free(buf)

		switch at {
		case poolAttr:
			rc = C.daos_pool_get_attr(hdl, 1, &attrName, &buf, &attrSize, nil)
		case contAttr:
			rc = C.daos_cont_get_attr(hdl, 1, &attrName, &buf, &attrSize, nil)
		default:
			return nil, errors.Errorf("unknown attr type %d", at)
		}
		if err := daosError(rc); err != nil {
			return nil, errors.Wrapf(err, "failed to get attribute %q", name)
		}

		attr.Value = C.GoString((*C.char)(buf))
	}

	return attr, nil
}

func setDaosAttribute(hdl C.daos_handle_t, at attrType, attr *attribute) error {
	if attr == nil {
		return errors.Errorf("nil %T", attr)
	}

	attrName := C.CString(attr.Name)
	defer freeString(attrName)
	attrValue := C.CString(attr.Value)
	defer freeString(attrValue)
	valueLen := C.uint64_t(len(attr.Value) + 1)

	var rc C.int
	switch at {
	case poolAttr:
		rc = C.daos_pool_set_attr(hdl, 1, &attrName,
			(*unsafe.Pointer)(unsafe.Pointer(&attrValue)), &valueLen, nil)
	case contAttr:
		rc = C.daos_cont_set_attr(hdl, 1, &attrName,
			(*unsafe.Pointer)(unsafe.Pointer(&attrValue)), &valueLen, nil)
	default:
		return errors.Errorf("unknown attr type %d", at)
	}

	return daosError(rc)
}

func delDaosAttribute(hdl C.daos_handle_t, at attrType, name string) error {
	attrName := C.CString(name)
	defer freeString(attrName)

	var rc C.int
	switch at {
	case poolAttr:
		rc = C.daos_pool_del_attr(hdl, 1, &attrName, nil)
	case contAttr:
		rc = C.daos_cont_del_attr(hdl, 1, &attrName, nil)
	default:
		return errors.Errorf("unknown attr type %d", at)
	}

	return daosError(rc)
}
