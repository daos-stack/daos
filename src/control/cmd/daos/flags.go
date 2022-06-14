//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
)

/*
#include "util.h"

extern int  obj_class_init(void);
extern void obj_class_fini(void);
*/
import "C"

func flagTestInit() (func(), error) {
	if err := daosError(C.obj_class_init()); err != nil {
		return nil, err
	}

	return func() {
		C.obj_class_fini()
	}, nil
}

type EpochFlag struct {
	Set   bool
	Value uint64
}

func (f *EpochFlag) String() string {
	return fmt.Sprintf("%#x", f.Value)
}

func (f *EpochFlag) UnmarshalFlag(fv string) (err error) {
	f.Value, err = strconv.ParseUint(fv, 0, 64)
	if err != nil {
		return errors.Errorf("failed to parse %q as uint64\n", fv)
	}

	f.Set = true
	return nil
}

type EpochRangeFlag struct {
	Set   bool
	Begin C.uint64_t
	End   C.uint64_t
}

func (f *EpochRangeFlag) String() string {
	return fmt.Sprintf("%#x-%#x", f.Begin, f.End)
}

func (f *EpochRangeFlag) UnmarshalFlag(fv string) error {
	comps := strings.Split(fv, "-")
	if len(comps) != 2 {
		return errors.Errorf("failed to parse %q as epoch range (expected A-B)", fv)
	}

	val, err := strconv.ParseUint(comps[0], 0, 64)
	if err != nil {
		return errors.Errorf("failed to parse %q as uint64", comps[0])
	}
	f.Begin = C.uint64_t(val)
	val, err = strconv.ParseUint(comps[1], 0, 64)
	if err != nil {
		return errors.Errorf("failed to parse %q as uint64", comps[1])
	}
	f.End = C.uint64_t(val)

	if f.Begin >= f.End {
		return errors.Errorf("range begin must be < end")
	}

	f.Set = true
	return nil
}

type ChunkSizeFlag struct {
	Set  bool
	Size C.uint64_t
}

func (f *ChunkSizeFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty chunk size")
	}

	size, err := humanize.ParseBytes(fv)
	if err != nil {
		return errors.Errorf("invalid chunk size %q", fv)
	}
	f.Size = C.uint64_t(size)

	f.Set = true
	return nil
}

func (f *ChunkSizeFlag) String() string {
	return humanize.IBytes(uint64(f.Size))
}

type ObjClassFlag struct {
	Set   bool
	Class C.uint
}

func (f *ObjClassFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty object class")
	}

	cObjClass := C.CString(fv)
	defer freeString(cObjClass)

	f.Class = (C.uint)(C.daos_oclass_name2id(cObjClass))
	if f.Class == C.OC_UNKNOWN {
		return errors.Errorf("unknown object class %q", fv)
	}

	f.Set = true
	return nil
}

func (f *ObjClassFlag) String() string {
	var oclass [32]C.char

	C.daos_oclass_id2name(f.Class, &oclass[0])
	return C.GoString(&oclass[0])
}

func (f *ObjClassFlag) Complete(match string) (comps []flags.Completion) {
	listStrSz := C.size_t(16 << 10)
	listStr := (*C.char)(C.malloc(listStrSz))

	defer freeString(listStr)
	C.daos_oclass_names_list(listStrSz, listStr)

	for _, name := range strings.Split(C.GoString(listStr), ",") {
		name = strings.TrimSpace(name)
		if strings.HasPrefix(name, match) {
			comps = append(comps, flags.Completion{Item: name})
		}
	}

	return
}

func makeOid(hi, lo uint64) C.daos_obj_id_t {
	return C.daos_obj_id_t{C.uint64_t(hi), C.uint64_t(lo)}
}

type OidFlag struct {
	Set bool
	Oid C.daos_obj_id_t
}

func (f *OidFlag) String() string {
	return fmt.Sprintf("%d.%d", f.Oid.hi, f.Oid.lo)
}

func (f *OidFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty oid")
	}

	parseErr := fmt.Sprintf("failed to parse %q as OID (expected HI.LO)", fv)
	comps := strings.Split(fv, ".")
	if len(comps) != 2 {
		return errors.New(parseErr)
	}

	val, err := strconv.ParseUint(comps[0], 10, 64)
	if err != nil {
		return errors.Wrap(err, parseErr)
	}
	f.Oid.hi = C.uint64_t(val)
	val, err = strconv.ParseUint(comps[1], 10, 64)
	if err != nil {
		return errors.Wrap(err, parseErr)
	}
	f.Oid.lo = C.uint64_t(val)

	f.Set = true
	return nil
}

type ConsModeFlag struct {
	Set  bool
	Mode C.uint32_t
}

func (f *ConsModeFlag) String() string {
	switch f.Mode {
	case C.DFS_RELAXED:
		return "relaxed"
	case C.DFS_BALANCED:
		return "balanced"
	default:
		return fmt.Sprintf("unknown mode %d", f.Mode)
	}
}

func (f *ConsModeFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty cons mode")
	}

	switch strings.ToLower(fv) {
	case "relaxed":
		f.Mode = C.DFS_RELAXED
	case "balanced":
		f.Mode = C.DFS_BALANCED
	default:
		return errors.Errorf("unknown consistency mode %q", fv)
	}

	f.Set = true
	return nil
}

type ContTypeFlag struct {
	Set  bool
	Type C.ushort
}

func (f *ContTypeFlag) String() string {
	cTypeStr := [16]C.char{}
	C.daos_unparse_ctype(f.Type, &cTypeStr[0])

	return C.GoString(&cTypeStr[0])
}

func (f *ContTypeFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty container type")
	}

	cTypeStr := C.CString(strings.ToUpper(fv))
	defer freeString(cTypeStr)

	C.daos_parse_ctype(cTypeStr, &f.Type)
	if f.Type == C.DAOS_PROP_CO_LAYOUT_UNKNOWN {
		return errors.Errorf("unknown container type %q", fv)
	}

	f.Set = true
	return nil
}
