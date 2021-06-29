//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/pkg/errors"
)

/*
#define D_LOGFAC	DD_FAC(client)

#include <sys/stat.h>
#include <daos.h>
#include <daos_fs.h>
*/
import "C"

type labelOrUUIDFlag struct {
	UUID  uuid.UUID
	Label string
}

func (f labelOrUUIDFlag) Empty() bool {
	return !f.HasLabel() && !f.HasUUID()
}

func (f labelOrUUIDFlag) HasLabel() bool {
	return f.Label != ""
}

func (f *labelOrUUIDFlag) SetLabel(label string) {
	f.Label = label
	return
}

func (f labelOrUUIDFlag) HasUUID() bool {
	return f.UUID != uuid.Nil
}

func (f labelOrUUIDFlag) String() string {
	switch {
	case f.HasLabel():
		return f.Label
	case f.HasUUID():
		return f.UUID.String()
	default:
		return "<no label or uuid set>"
	}
}

func (f *labelOrUUIDFlag) UnmarshalFlag(fv string) error {
	uuid, err := uuid.Parse(fv)
	if err == nil {
		f.UUID = uuid
		return nil
	}

	f.Label = fv
	return nil
}

type epochRangeFlag struct {
	set   bool
	begin uint64
	end   uint64
}

func (er *epochRangeFlag) String() string {
	return fmt.Sprintf("%d-%d", er.begin, er.end)
}

func (er *epochRangeFlag) UnmarshalFlag(fv string) error {
	n, err := fmt.Sscanf(fv, "%d-%d", &er.begin, &er.end)
	if err != nil {
		return err
	}
	if n != 2 {
		return errors.Errorf("range=%q must be in A-B form", fv)
	}
	if er.begin >= er.end {
		return errors.Errorf("range begin must be < end")
	}

	er.set = true
	return nil
}

type chunkSizeFlag struct {
	set  bool
	size C.uint64_t
}

func (c *chunkSizeFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty chunk size")
	}

	size, err := humanize.ParseBytes(fv)
	if err != nil {
		return err
	}
	c.size = C.uint64_t(size)

	c.set = true
	return nil
}

type objClassFlag struct {
	set   bool
	class C.ushort
}

func (oc *objClassFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty object class")
	}

	cObjClass := C.CString(fv)
	defer freeString(cObjClass)

	oc.class = (C.ushort)(C.daos_oclass_name2id(cObjClass))
	if oc.class == C.OC_UNKNOWN {
		return errors.Errorf("unknown object class %q", fv)
	}

	oc.set = true
	return nil
}

type oidFlag struct {
	set bool
	oid C.daos_obj_id_t
}

func (f *oidFlag) String() string {
	return fmt.Sprintf("%d.%d", f.oid.hi, f.oid.lo)
}

func (f *oidFlag) UnmarshalFlag(fv string) error {
	parseErr := fmt.Sprintf("failed to parse %q as OID (expected HI.LO)", fv)
	comps := strings.Split(fv, ".")
	if len(comps) != 2 {
		return errors.New(parseErr)
	}

	val, err := strconv.ParseUint(comps[0], 10, 64)
	if err != nil {
		return errors.Wrap(err, parseErr)
	}
	f.oid.hi = C.uint64_t(val)
	val, err = strconv.ParseUint(comps[1], 10, 64)
	if err != nil {
		return errors.Wrap(err, parseErr)
	}
	f.oid.lo = C.uint64_t(val)

	f.set = true
	return nil
}

type consModeFlag struct {
	set  bool
	mode C.uint32_t
}

func (f *consModeFlag) String() string {
	switch f.mode {
	case C.DFS_RELAXED:
		return "relaxed"
	case C.DFS_BALANCED:
		return "balanced"
	default:
		return fmt.Sprintf("unknown mode %d", f.mode)
	}
}

func (f *consModeFlag) UnmarshalFlag(fv string) error {
	switch strings.ToLower(fv) {
	case "relaxed":
		f.mode = C.DFS_RELAXED
	case "balanced":
		f.mode = C.DFS_BALANCED
	default:
		return errors.Errorf("unknown consistency mode %q", fv)
	}

	f.set = true
	return nil
}
