//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"strings"
	"unsafe"

	"github.com/dustin/go-humanize"
	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
)

/*
#include <daos.h>
#include <daos/multihash.h>
#include <daos/compression.h>
#include <daos/cipher.h>

uint64_t
daos_prop_co_status_val(uint32_t status, uint32_t ver)
{
	return DAOS_PROP_CO_STATUS_VAL(status, ver);
}

void
set_dpe_str(struct daos_prop_entry *dpe, d_string_t str)
{
	dpe->dpe_str = str;
}

void
set_dpe_val(struct daos_prop_entry *dpe, uint64_t val)
{
	dpe->dpe_val = val;
}

void
set_dpe_val_ptr(struct daos_prop_entry *dpe, void *val_ptr)
{
	dpe->dpe_val_ptr = val_ptr;
}
*/
import "C"

type propHdlr func(*C.struct_daos_prop_entry, string) error

// propHdlrs defines a map of property names to closures
// that handle parsing/setting the property value. This odd
// construction allows us to easily implement the flags.Completer
// interface and generate completions for the property names
// without having to maintain separate lists.
//
// TODO: Figure out how to add completions for property values,
// where applicable.
var propHdlrs = map[string]propHdlr{
	"label": func(e *C.struct_daos_prop_entry, v string) error {
		C.set_dpe_str(e, C.CString(v))
		e.dpe_type = C.DAOS_PROP_CO_LABEL
		return nil
	},
	"cksum": func(e *C.struct_daos_prop_entry, v string) error {
		csumVal := C.CString(v)
		defer C.free(unsafe.Pointer(csumVal))
		csumType := C.daos_str2csumcontprop(csumVal)

		if csumType < 0 {
			return errors.Errorf("unknown checksum type %q", v)
		}

		C.set_dpe_val(e, C.uint64_t(csumType))
		e.dpe_type = C.DAOS_PROP_CO_CSUM
		return nil
	},
	"cksum_size": func(e *C.struct_daos_prop_entry, v string) error {
		size, err := humanize.ParseBytes(v)
		if err != nil {
			return errors.Wrapf(err, "unable to parse checksum size %q", v)
		}

		C.set_dpe_val(e, C.uint64_t(size))
		e.dpe_type = C.DAOS_PROP_CO_CSUM_CHUNK_SIZE
		return nil
	},
	"srv_cksum": func(e *C.struct_daos_prop_entry, v string) error {
		switch strings.ToLower(v) {
		case "on":
			C.set_dpe_val(e, C.DAOS_PROP_CO_CSUM_SV_ON)
		case "off":
			C.set_dpe_val(e, C.DAOS_PROP_CO_CSUM_SV_OFF)
		default:
			return errors.Errorf("invalid srv_cksum value %q (must be on/off)", v)
		}

		e.dpe_type = C.DAOS_PROP_CO_CSUM_SERVER_VERIFY
		return nil
	},
	"dedup": func(e *C.struct_daos_prop_entry, v string) error {
		switch strings.ToLower(v) {
		case "off":
			C.set_dpe_val(e, C.DAOS_PROP_CO_DEDUP_OFF)
		case "memcmp":
			C.set_dpe_val(e, C.DAOS_PROP_CO_DEDUP_MEMCMP)
		case "hash":
			C.set_dpe_val(e, C.DAOS_PROP_CO_DEDUP_HASH)
		default:
			return errors.Errorf("invalid dedup value %q", v)
		}

		e.dpe_type = C.DAOS_PROP_CO_DEDUP
		return nil
	},
	"dedup_threshold": func(e *C.struct_daos_prop_entry, v string) error {
		size, err := humanize.ParseBytes(v)
		if err != nil {
			return errors.Wrapf(err, "unable to parse checksum size %q", v)
		}

		C.set_dpe_val(e, C.uint64_t(size))
		e.dpe_type = C.DAOS_PROP_CO_DEDUP_THRESHOLD
		return nil
	},
	"compression": func(e *C.struct_daos_prop_entry, v string) error {
		compVal := C.CString(v)
		defer C.free(unsafe.Pointer(compVal))
		compType := C.daos_str2compresscontprop(compVal)

		if compType < 0 {
			return errors.Errorf("unknown compression type %q", v)
		}

		C.set_dpe_val(e, C.uint64_t(compType))
		e.dpe_type = C.DAOS_PROP_CO_COMPRESS
		return nil
	},
	"encryption": func(e *C.struct_daos_prop_entry, v string) error {
		encVal := C.CString(v)
		defer C.free(unsafe.Pointer(encVal))
		encType := C.daos_str2encryptcontprop(encVal)

		if encType < 0 {
			return errors.Errorf("unknown encryption type %q", v)
		}

		C.set_dpe_val(e, C.uint64_t(encType))
		e.dpe_type = C.DAOS_PROP_CO_ENCRYPT
		return nil
	},
	"rf": func(e *C.struct_daos_prop_entry, v string) error {
		switch v {
		case "0":
			C.set_dpe_val(e, C.DAOS_PROP_CO_REDUN_RF0)
		case "1":
			C.set_dpe_val(e, C.DAOS_PROP_CO_REDUN_RF1)
		case "2":
			C.set_dpe_val(e, C.DAOS_PROP_CO_REDUN_RF2)
		case "3":
			C.set_dpe_val(e, C.DAOS_PROP_CO_REDUN_RF3)
		case "4":
			C.set_dpe_val(e, C.DAOS_PROP_CO_REDUN_RF4)
		default:
			return errors.New("rf must be an integer between 0 and 4")
		}

		e.dpe_type = C.DAOS_PROP_CO_REDUN_FAC
		return nil
	},
	"status": func(e *C.struct_daos_prop_entry, v string) error {
		if v != "healthy" {
			return errors.New("status prop value can only be 'healthy' to clear UNCLEAN status")
		}

		C.set_dpe_val(e, C.daos_prop_co_status_val(C.DAOS_PROP_CO_HEALTHY, 0))
		e.dpe_type = C.DAOS_PROP_CO_STATUS
		return nil
	},
}

type PropertiesFlag struct {
	props *C.daos_prop_t
}

func (f *PropertiesFlag) Complete(match string) (comps []flags.Completion) {
	for name := range propHdlrs {
		if strings.HasPrefix(name, match) {
			comps = append(comps, flags.Completion{Item: name})
		}
	}

	return
}

func (f *PropertiesFlag) UnmarshalFlag(fv string) (err error) {
	f.props = C.daos_prop_alloc(C.DAOS_PROP_ENTRIES_MAX_NR)
	f.props.dpp_nr = 0
	defer func() {
		if err != nil {
			f.Cleanup()
		}
	}()

	// Create a Go slice backed by the props array for easier
	// iteration.
	numEntries := int(C.DAOS_PROP_ENTRIES_MAX_NR)
	entries := (*[1 << 30]C.struct_daos_prop_entry)(unsafe.Pointer(f.props.dpp_entries))[:numEntries:numEntries]

	maxNameLen := 20
	maxValueLen := C.DAOS_PROP_LABEL_MAX_LEN

	for i, propStr := range strings.Split(fv, ",") {
		keyVal := strings.Split(propStr, ":")
		if len(keyVal) != 2 {
			return errors.Errorf("invalid property %q (must be name:val)", propStr)
		}

		name := strings.TrimSpace(keyVal[0])
		value := strings.TrimSpace(keyVal[1])
		if len(name) == 0 {
			return errors.New("name must not be empty")
		}
		if len(name) > maxNameLen {
			return errors.Errorf("name too long (%d > %d)",
				len(name), maxNameLen)
		}
		if len(value) == 0 {
			return errors.New("value must not be empty")
		}
		if len(value) > maxValueLen {
			return errors.Errorf("value too long (%d > %d)",
				len(value), maxValueLen)
		}

		hdlr, found := propHdlrs[strings.ToLower(name)]
		if !found {
			return &flags.Error{
				Type:    flags.ErrInvalidChoice,
				Message: fmt.Sprintf("unknown property %q", name),
			}
		}

		if err = hdlr(&entries[i], value); err != nil {
			return
		}

		f.props.dpp_nr++
	}

	return nil
}

func (f *PropertiesFlag) Cleanup() {
	if f.props == nil {
		return
	}

	f.props.dpp_nr = C.DAOS_PROP_ENTRIES_MAX_NR
	C.daos_prop_free(f.props)
}
