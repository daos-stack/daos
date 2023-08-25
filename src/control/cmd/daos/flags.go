//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
	"github.com/daos-stack/daos/src/control/lib/dfs"
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
		return err
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
	Class daosAPI.ObjectClass
}

func (f *ObjClassFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty object class")
	}

	if err := f.Class.FromString(fv); err != nil {
		return err
	}

	f.Set = true
	return nil
}

func (f *ObjClassFlag) String() string {
	return f.Class.String()
}

func makeOid(hi, lo uint64) C.daos_obj_id_t {
	return C.daos_obj_id_t{C.uint64_t(hi), C.uint64_t(lo)}
}

type OidFlag struct {
	Set bool
	Oid C.daos_obj_id_t
}

func oidString(oid C.daos_obj_id_t) string {
	return fmt.Sprintf("%d.%d", oid.hi, oid.lo)
}

func (f *OidFlag) String() string {
	return oidString(f.Oid)
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
	Mode dfs.ConsistencyMode
}

func (f *ConsModeFlag) String() string {
	return f.Mode.String()
}

func (f *ConsModeFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty cons mode")
	}

	if err := f.Mode.FromString(fv); err != nil {
		return err
	}

	f.Set = true
	return nil
}

type ContTypeFlag struct {
	Set  bool
	Type daosAPI.ContainerLayout
}

func (f *ContTypeFlag) String() string {
	return f.Type.String()
}

func (f *ContTypeFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty container type")
	}

	if err := f.Type.FromString(fv); err != nil {
		return err
	}

	f.Set = true
	return nil
}

type FsCheckFlag struct {
	Set   bool
	Flags C.uint64_t
}

func (f *FsCheckFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty filesystem check flags")
	}

	for _, cflag := range strings.Split(fv, ",") {
		switch strings.TrimSpace(strings.ToLower(cflag)) {
		case "print":
			f.Flags |= C.DFS_CHECK_PRINT
		case "evict":
			f.Flags |= C.DFS_CHECK_EVICT_ALL
		case "remove":
			f.Flags |= C.DFS_CHECK_REMOVE
		case "relink":
			f.Flags |= C.DFS_CHECK_RELINK
		case "verify":
			f.Flags |= C.DFS_CHECK_VERIFY
		default:
			return errors.Errorf("unknown filesystem check flags: %q", fv)
		}
	}

	f.Set = true
	return nil
}
