//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"sort"
	"strings"
	"unsafe"

	"github.com/dustin/go-humanize"
	flags "github.com/jessevdk/go-flags"
)

/*
#include <daos.h>
#include <daos/multihash.h>
#include <daos/compression.h>
#include <daos/cipher.h>

// cgo is unable to work directly with preprocessor macros
// so we have to provide these glue helpers.
uint64_t
daos_prop_co_status_val(uint32_t status, uint32_t ver)
{
	return DAOS_PROP_CO_STATUS_VAL(status, ver);
}

// cgo is unable to work directly with unions, so we have
// to provide these glue helpers.
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

// propHdlrs defines a map of property names to handlers that
// take care of parsing the value and setting it. This odd construction
// allows us to maintain type-safe lists of valid labels and resolvable
// string properties in one place, and also enables user-friendly
// command completion.
//
// Most new features requiring a change to the property handling
// should only need to modify this map.
var propHdlrs = propHdlrMap{
	"label": {
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			C.set_dpe_str(e, C.CString(v))
			e.dpe_type = C.DAOS_PROP_CO_LABEL
			return nil
		},
		nil,
	},
	"cksum": {
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("cksum", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"off":     cksumHdlr,
			"adler32": cksumHdlr,
			"crc16":   cksumHdlr, "crc32": cksumHdlr, "crc64": cksumHdlr,
			"sha1": cksumHdlr, "sha256": cksumHdlr, "sha512": cksumHdlr,
		},
	},
	"cksum_size": {
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			size, err := humanize.ParseBytes(v)
			if err != nil {
				return propError("invalid cksum_size %q (try N<unit>)", v)
			}

			C.set_dpe_val(e, C.uint64_t(size))
			e.dpe_type = C.DAOS_PROP_CO_CSUM_CHUNK_SIZE
			return nil
		},
		nil,
	},
	"srv_cksum": {
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("srv_cksum", v)
			if err != nil {
				return err
			}

			e.dpe_type = C.DAOS_PROP_CO_CSUM_SERVER_VERIFY
			return vh(e, v)
		},
		valHdlrMap{
			"on":  setDpeVal(C.DAOS_PROP_CO_CSUM_SV_ON),
			"off": setDpeVal(C.DAOS_PROP_CO_CSUM_SV_OFF),
		},
	},
	"dedup": {
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("dedup", v)
			if err != nil {
				return err
			}

			e.dpe_type = C.DAOS_PROP_CO_DEDUP
			return vh(e, v)
		},
		valHdlrMap{
			"off":    setDpeVal(C.DAOS_PROP_CO_DEDUP_OFF),
			"memcmp": setDpeVal(C.DAOS_PROP_CO_DEDUP_MEMCMP),
			"hash":   setDpeVal(C.DAOS_PROP_CO_DEDUP_HASH),
		},
	},
	"dedup_threshold": {
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			size, err := humanize.ParseBytes(v)
			if err != nil {
				return propError("invalid dedup_threshold %q (try N<unit>)", v)
			}

			C.set_dpe_val(e, C.uint64_t(size))
			e.dpe_type = C.DAOS_PROP_CO_DEDUP_THRESHOLD
			return nil
		},
		nil,
	},
	"compression": {
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("compression", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"off":  compressHdlr,
			"lz4":  compressHdlr,
			"gzip": compressHdlr, "gzip1": compressHdlr,
			"gzip2": compressHdlr, "gzip3": compressHdlr,
			"gzip4": compressHdlr, "gzip5": compressHdlr,
			"gzip6": compressHdlr, "gzip7": compressHdlr,
			"gzip8": compressHdlr, "gzip9": compressHdlr,
		},
	},
	"encryption": {
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("encryption", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"off":        encryptHdlr,
			"aes-xts128": encryptHdlr, "aes-xts256": encryptHdlr,
			"aes-cbc128": encryptHdlr, "aes-cbc192": encryptHdlr,
			"aes-cbc256": encryptHdlr,
			"aes-gcm128": encryptHdlr, "aes-gcm256": encryptHdlr,
		},
	},
	"rf": {
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("rf", v)
			if err != nil {
				return err
			}

			e.dpe_type = C.DAOS_PROP_CO_REDUN_FAC
			return vh(e, v)
		},
		valHdlrMap{
			"0": setDpeVal(C.DAOS_PROP_CO_REDUN_RF0),
			"1": setDpeVal(C.DAOS_PROP_CO_REDUN_RF1),
			"2": setDpeVal(C.DAOS_PROP_CO_REDUN_RF2),
			"3": setDpeVal(C.DAOS_PROP_CO_REDUN_RF3),
			"4": setDpeVal(C.DAOS_PROP_CO_REDUN_RF4),
		},
	},
	"status": {
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("status", v)
			if err != nil {
				return err
			}

			e.dpe_type = C.DAOS_PROP_CO_STATUS
			return vh(e, v)
		},
		valHdlrMap{
			"healthy": setDpeVal(C.daos_prop_co_status_val(C.DAOS_PROP_CO_HEALTHY, 0)),
		},
	},
}

// NB: Most feature work should not require modification to the code
// below.

type entryHdlr func(*propHdlr, *C.struct_daos_prop_entry, string) error
type valHdlr func(*C.struct_daos_prop_entry, string) error

type valHdlrMap map[string]valHdlr

func (vhm valHdlrMap) keys() (keys []string) {
	keys = make([]string, 0, len(vhm))
	for key := range vhm {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return
}

func (vhm valHdlrMap) get(prop, value string) (valHdlr, error) {
	vh, found := vhm[strings.ToLower(value)]
	if !found {
		return nil, propError(
			"invalid choice %q for %s (valid: %s)",
			value, prop, strings.Join(vhm.keys(), ","))
	}
	return vh, nil
}

type propHdlr struct {
	nameHdlr entryHdlr
	valHdlrs valHdlrMap
}

func (ph *propHdlr) execute(e *C.struct_daos_prop_entry, v string) error {
	if ph.nameHdlr == nil {
		return propError("no name handler set")
	}

	return ph.nameHdlr(ph, e, v)
}

type propHdlrMap map[string]*propHdlr

func (phm propHdlrMap) keys() (keys []string) {
	keys = make([]string, 0, len(phm))
	for key := range phm {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return
}

func (phm propHdlrMap) get(prop string) (*propHdlr, error) {
	ph, found := phm[strings.ToLower(prop)]
	if !found {
		return nil, propError(
			"unknown property %q (valid: %s)",
			prop, strings.Join(phm.keys(), ","))
	}
	return ph, nil
}

func cksumHdlr(e *C.struct_daos_prop_entry, v string) error {
	csumVal := C.CString(v)
	defer C.free(unsafe.Pointer(csumVal))
	csumType := C.daos_str2csumcontprop(csumVal)

	if csumType < 0 {
		return propError("unknown checksum type %q", v)
	}

	C.set_dpe_val(e, C.uint64_t(csumType))
	e.dpe_type = C.DAOS_PROP_CO_CSUM
	return nil
}

func compressHdlr(e *C.struct_daos_prop_entry, v string) error {
	compVal := C.CString(v)
	defer C.free(unsafe.Pointer(compVal))
	compType := C.daos_str2compresscontprop(compVal)

	if compType < 0 {
		return propError("unknown compression type %q", v)
	}

	C.set_dpe_val(e, C.uint64_t(compType))
	e.dpe_type = C.DAOS_PROP_CO_COMPRESS
	return nil
}

func encryptHdlr(e *C.struct_daos_prop_entry, v string) error {
	encVal := C.CString(v)
	defer C.free(unsafe.Pointer(encVal))
	encType := C.daos_str2encryptcontprop(encVal)

	if encType < 0 {
		return propError("unknown encryption type %q", v)
	}

	C.set_dpe_val(e, C.uint64_t(encType))
	e.dpe_type = C.DAOS_PROP_CO_ENCRYPT
	return nil
}

func setDpeVal(v C.uint64_t) valHdlr {
	return func(e *C.struct_daos_prop_entry, _ string) error {
		C.set_dpe_val(e, v)
		return nil
	}
}

func propError(fs string, args ...interface{}) *flags.Error {
	return &flags.Error{
		Message: fmt.Sprintf("--properties: "+fs, args...),
	}
}

// PropertiesFlag implements the flags.Unmarshaler and flags.Completer
// interfaces in order to provide a custom flag type for converting
// command-line arguments into a *C.daos_prop_t array suitable for
// creating a container. Use the SetPropertiesFlag type for setting
// properties on an existing container.
type PropertiesFlag struct {
	props *C.daos_prop_t

	allowedProps map[string]struct{}
}

func (f *PropertiesFlag) setAllowedProps(props ...string) {
	f.allowedProps = make(map[string]struct{})
	for _, prop := range props {
		f.allowedProps[prop] = struct{}{}
	}
}

func (f *PropertiesFlag) isAllowedProp(prop string) bool {
	// If the list of allowed props is not set, default
	// to allowing all.
	if len(f.allowedProps) == 0 {
		return true
	}

	_, allowed := f.allowedProps[prop]
	return allowed
}

func (f *PropertiesFlag) Complete(match string) (comps []flags.Completion) {
	var prefix string
	propPairs := strings.Split(match, ",")
	if len(propPairs) > 1 {
		match = propPairs[len(propPairs)-1:][0]
		prefix = strings.Join(propPairs[0:len(propPairs)-1], ",")
		prefix += ","
	}

	for propKey, hdlr := range propHdlrs {
		if !f.isAllowedProp(propKey) {
			continue
		}

		if len(hdlr.valHdlrs) == 0 || !strings.Contains(match, ":") {
			if strings.HasPrefix(propKey, match) {
				comps = append(comps, flags.Completion{Item: prefix + propKey + ":"})
			}
			continue
		}

		for valKey := range hdlr.valHdlrs {
			propVal := propKey + ":" + valKey
			if strings.HasPrefix(propVal, match) {
				comps = append(comps, flags.Completion{Item: valKey})
			}
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
			return propError("invalid property %q (must be name:val)", propStr)
		}

		name := strings.TrimSpace(keyVal[0])
		value := strings.TrimSpace(keyVal[1])
		if len(name) == 0 {
			return propError("name must not be empty")
		}
		if len(name) > maxNameLen {
			return propError("name too long (%d > %d)",
				len(name), maxNameLen)
		}
		if len(value) == 0 {
			return propError("value must not be empty")
		}
		if len(value) > maxValueLen {
			return propError("value too long (%d > %d)",
				len(value), maxValueLen)
		}

		var hdlr *propHdlr
		hdlr, err = propHdlrs.get(name)
		if err != nil {
			return
		}

		if !f.isAllowedProp(name) {
			return propError("prop %q is not allowed to be set",
				name)
		}

		if err = hdlr.execute(&entries[i], value); err != nil {
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

// SetPropertiesFlag embeds the base PropertiesFlag struct to
// compose a flag that is used for setting properties on a
// container. It is intended to be used where only a subset of
// properties are valid for setting.
type SetPropertiesFlag struct {
	PropertiesFlag
}

func (f *SetPropertiesFlag) Complete(match string) []flags.Completion {
	f.setAllowedProps("label", "status")

	return f.PropertiesFlag.Complete(match)
}

func (f *SetPropertiesFlag) UnmarshalFlag(fv string) error {
	f.setAllowedProps("label", "status")

	if err := f.PropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	return nil
}
