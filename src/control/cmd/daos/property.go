//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"sort"
	"strconv"
	"strings"
	"unsafe"

	"github.com/dustin/go-humanize"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

/*
#define D_LOGFAC	DD_FAC(client)

#include <daos.h>
#include <daos/common.h>
#include <daos/multihash.h>
#include <daos/compression.h>
#include <daos/cipher.h>
#include <daos/object.h>

#include "property.h"
*/
import "C"

type propHdlr struct {
	// dpeType holds the property type (must be set).
	dpeType C.enum_daos_cont_props
	// shortDesc holds a short description of the property
	// to be used in human-readable output.
	shortDesc string
	// nameHdlr holds a closure for processing the entry.
	// Optional; if not set, the property is read-only.
	nameHdlr entryHdlr
	// valHdlrs defines a map of string-based property values
	// that should be resolved into one of the entry's
	// union values. Optional.
	valHdlrs valHdlrMap
	// toString defines a closure for converting the
	// entry's value into a string.
	toString entryStringer
}

// propHdlrs defines a map of property names to handlers that
// take care of parsing the value and setting it. This odd construction
// allows us to maintain a type-safe set of valid property names and
// string properties in one place, and also enables user-friendly
// command completion.
//
// Most new features requiring a change to the property handling
// should only need to modify this map.
//
// The structure of an entry is as follows:
//	"key": {		           // used as the --property name
//		C.DAOS_PROP_ENUM_VAL,      // identify the property type
//		"short description",       // human-readable (short) description
//		closure of type entryHdlr, // process set entry (optional for read-only)
//		map[string]valHdlr{	   // optional map of string value processors
//			"key": closure of type valHdlr, // process set value
//		},
//		closure of type entryStringer // optional pretty-printer
// 	},
var propHdlrs = propHdlrMap{
	"label": {
		C.DAOS_PROP_CO_LABEL,
		"Label",
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			cStr := C.CString(v)
			C.set_dpe_dupe_str(e, cStr, C.int(len(v)+1))
			freeString(cStr)
			return nil
		},
		nil,
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			if C.get_dpe_str(e) == nil {
				return "container label not set"
			}
			return strValStringer(e, name)
		},
	},
	"cksum": {
		C.DAOS_PROP_CO_CSUM,
		"Checksum",
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
			"crc16":   cksumHdlr,
			"crc32":   cksumHdlr,
			"crc64":   cksumHdlr,
			"sha1":    cksumHdlr,
			"sha256":  cksumHdlr,
			"sha512":  cksumHdlr,
		},
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			dpeVal := C.int(C.get_dpe_val(e))
			if dpeVal == C.DAOS_PROP_CO_CSUM_OFF {
				return "off"
			}

			var csum *C.struct_hash_ft
			csum = C.daos_mhash_type2algo(
				C.daos_contprop2hashtype(dpeVal))
			if csum == nil {
				return propInvalidValue(e, name)
			}
			return C.GoString(csum.cf_name)
		},
	},
	"cksum_size": {
		C.DAOS_PROP_CO_CSUM_CHUNK_SIZE,
		"Checksum Chunk Size",
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			size, err := humanize.ParseBytes(v)
			if err != nil {
				return propError("invalid cksum_size %q (try N<unit>)", v)
			}

			C.set_dpe_val(e, C.uint64_t(size))
			return nil
		},
		nil,
		humanSizeStringer,
	},
	"srv_cksum": {
		C.DAOS_PROP_CO_CSUM_SERVER_VERIFY,
		"Server Checksumming",
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("srv_cksum", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"on":  setDpeVal(C.DAOS_PROP_CO_CSUM_SV_ON),
			"off": setDpeVal(C.DAOS_PROP_CO_CSUM_SV_OFF),
		},
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			switch C.get_dpe_val(e) {
			case C.DAOS_PROP_CO_CSUM_SV_OFF:
				return "off"
			case C.DAOS_PROP_CO_CSUM_SV_ON:
				return "on"
			default:
				return propInvalidValue(e, name)
			}
		},
	},
	"dedup": {
		C.DAOS_PROP_CO_DEDUP,
		"Deduplication",
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("dedup", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"off":    setDpeVal(C.DAOS_PROP_CO_DEDUP_OFF),
			"memcmp": setDpeVal(C.DAOS_PROP_CO_DEDUP_MEMCMP),
			"hash":   setDpeVal(C.DAOS_PROP_CO_DEDUP_HASH),
		},
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			switch C.get_dpe_val(e) {
			case C.DAOS_PROP_CO_DEDUP_OFF:
				return "off"
			case C.DAOS_PROP_CO_DEDUP_MEMCMP:
				return "memcmp"
			case C.DAOS_PROP_CO_DEDUP_HASH:
				return "hash"
			default:
				return propInvalidValue(e, name)
			}
		},
	},
	"dedup_threshold": {
		C.DAOS_PROP_CO_DEDUP_THRESHOLD,
		"Dedupe Threshold",
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			size, err := humanize.ParseBytes(v)
			if err != nil {
				return propError("invalid dedup_threshold %q (try N<unit>)", v)
			}

			C.set_dpe_val(e, C.uint64_t(size))
			return nil
		},
		nil,
		humanSizeStringer,
	},
	"compression": {
		C.DAOS_PROP_CO_COMPRESS,
		"Compression",
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("compression", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"off":      compressHdlr,
			"lz4":      compressHdlr,
			"deflate":  compressHdlr,
			"deflate1": compressHdlr,
			"deflate2": compressHdlr,
			"deflate3": compressHdlr,
			"deflate4": compressHdlr,
		},
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			dpeVal := C.int(C.get_dpe_val(e))
			if dpeVal == C.DAOS_PROP_CO_CSUM_OFF {
				return "off"
			}

			qatPreferred := C.bool(true)
			var algo *C.struct_compress_ft
			algo = C.daos_compress_type2algo(
				C.daos_contprop2compresstype(dpeVal), qatPreferred)
			if algo == nil {
				return propInvalidValue(e, name)
			}
			return C.GoString(algo.cf_name)
		},
	},
	"encryption": {
		C.DAOS_PROP_CO_ENCRYPT,
		"Encryption",
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("encryption", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"off":        encryptHdlr,
			"aes-xts128": encryptHdlr,
			"aes-xts256": encryptHdlr,
			"aes-cbc128": encryptHdlr,
			"aes-cbc192": encryptHdlr,
			"aes-cbc256": encryptHdlr,
			"aes-gcm128": encryptHdlr,
			"aes-gcm256": encryptHdlr,
		},
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			dpeVal := C.int(C.get_dpe_val(e))
			if dpeVal == C.DAOS_PROP_CO_ENCRYPT_OFF {
				return "off"
			}

			var algo *C.struct_cipher_ft
			algo = C.daos_cipher_type2algo(
				C.daos_contprop2ciphertype(dpeVal))
			if algo == nil {
				return propInvalidValue(e, name)
			}
			return C.GoString(algo.cf_name)
		},
	},
	"rf": {
		C.DAOS_PROP_CO_REDUN_FAC,
		"Redundancy Factor",
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("rf", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"0": setDpeVal(C.DAOS_PROP_CO_REDUN_RF0),
			"1": setDpeVal(C.DAOS_PROP_CO_REDUN_RF1),
			"2": setDpeVal(C.DAOS_PROP_CO_REDUN_RF2),
			"3": setDpeVal(C.DAOS_PROP_CO_REDUN_RF3),
			"4": setDpeVal(C.DAOS_PROP_CO_REDUN_RF4),
		},
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			switch C.get_dpe_val(e) {
			case C.DAOS_PROP_CO_REDUN_RF0:
				return "rf0"
			case C.DAOS_PROP_CO_REDUN_RF1:
				return "rf1"
			case C.DAOS_PROP_CO_REDUN_RF2:
				return "rf2"
			case C.DAOS_PROP_CO_REDUN_RF3:
				return "rf3"
			case C.DAOS_PROP_CO_REDUN_RF4:
				return "rf4"
			default:
				return propInvalidValue(e, name)
			}
		},
	},
	"status": {
		C.DAOS_PROP_CO_STATUS,
		"Health",
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("status", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"healthy": setDpeVal(C.daos_prop_co_status_val(C.DAOS_PROP_CO_HEALTHY, 0, 0)),
		},
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			coInt := C.get_dpe_val(e)
			var coStatus C.struct_daos_co_status

			C.daos_prop_val_2_co_status(coInt, &coStatus)
			switch coStatus.dcs_status {
			case C.DAOS_PROP_CO_HEALTHY:
				return "HEALTHY"
			case C.DAOS_PROP_CO_UNCLEAN:
				return "UNCLEAN"
			default:
				return propInvalidValue(e, name)
			}
		},
	},
	"ec_cell": {
		C.DAOS_PROP_CO_EC_CELL_SZ,
		"EC Cell Size",
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			size, err := strconv.ParseUint(v, 10, 64)
			if err != nil {
				return errors.Wrapf(err,
					"unable to parse EC cell size %q", v)
			}

			if !C.daos_ec_cs_valid(C.uint32_t(size)) {
				return errors.Errorf("invalid EC cell size %d", size)
			}

			C.set_dpe_val(e, C.uint64_t(size))
			return nil
		},
		nil,
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}

			size := C.get_dpe_val(e)
			if !C.daos_ec_cs_valid(C.uint32_t(size)) {
				return fmt.Sprintf("invalid size %d", size)
			}
			return humanSizeStringer(e, name)
		},
	},
	// Read-only properties here for use by get-property.
	"layout_type": {
		C.DAOS_PROP_CO_LAYOUT_TYPE,
		"Layout Type",
		nil, nil,
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			var loStr [10]C.char
			loInt := C.ushort(C.get_dpe_val(e))

			C.daos_unparse_ctype(C.ushort(C.get_dpe_val(e)), &loStr[0])
			return fmt.Sprintf("%s (%d)",
				C.GoString((*C.char)(unsafe.Pointer(&loStr[0]))), loInt)
		},
	},
	"layout_version": {
		C.DAOS_PROP_CO_LAYOUT_VER,
		"Layout Version",
		nil, nil,
		uintStringer,
	},
	"rf_lvl": {
		C.DAOS_PROP_CO_REDUN_LVL,
		"Redundancy Level",
		nil, nil,
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}

			lvl := C.get_dpe_val(e)
			switch lvl {
			case C.DAOS_PROP_CO_REDUN_RANK:
				return fmt.Sprintf("rank (%d)", lvl)
			default:
				return fmt.Sprintf("(%d)", lvl)
			}
		},
	},
	"max_snapshot": {
		C.DAOS_PROP_CO_SNAPSHOT_MAX,
		"Max Snapshot",
		nil, nil,
		uintStringer,
	},
	"alloc_oid": {
		C.DAOS_PROP_CO_ALLOCED_OID,
		"Highest Allocated OID",
		nil, nil,
		uintStringer,
	},
	"owner": {
		C.DAOS_PROP_CO_OWNER,
		"Owner",
		nil, nil,
		strValStringer,
	},
	"group": {
		C.DAOS_PROP_CO_OWNER_GROUP,
		"Group",
		nil, nil,
		strValStringer,
	},
}

// NB: Most feature work should not require modification to the code
// below.

const (
	maxNameLen  = 20 // arbitrary; came from C code
	maxValueLen = C.DAOS_PROP_LABEL_MAX_LEN
)

type entryHdlr func(*propHdlr, *C.struct_daos_prop_entry, string) error
type valHdlr func(*C.struct_daos_prop_entry, string) error
type entryStringer func(*C.struct_daos_prop_entry, string) string

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
	defer freeString(csumVal)
	csumType := C.daos_str2csumcontprop(csumVal)

	if csumType < 0 {
		return propError("unknown checksum type %q", v)
	}

	C.set_dpe_val(e, C.uint64_t(csumType))
	return nil
}

func compressHdlr(e *C.struct_daos_prop_entry, v string) error {
	compVal := C.CString(v)
	defer freeString(compVal)
	compType := C.daos_str2compresscontprop(compVal)

	if compType < 0 {
		return propError("unknown compression type %q", v)
	}

	C.set_dpe_val(e, C.uint64_t(compType))
	return nil
}

func encryptHdlr(e *C.struct_daos_prop_entry, v string) error {
	encVal := C.CString(v)
	defer freeString(encVal)
	encType := C.daos_str2encryptcontprop(encVal)

	if encType < 0 {
		return propError("unknown encryption type %q", v)
	}

	C.set_dpe_val(e, C.uint64_t(encType))
	return nil
}

func setDpeVal(v C.uint64_t) valHdlr {
	return func(e *C.struct_daos_prop_entry, _ string) error {
		C.set_dpe_val(e, v)
		return nil
	}
}

func propNotFound(name string) string {
	return fmt.Sprintf("property %q not found", name)
}

func propInvalidValue(e *C.struct_daos_prop_entry, name string) string {
	return fmt.Sprintf("property %q: invalid value %x",
		name, C.get_dpe_val(e))
}

func propError(fs string, args ...interface{}) *flags.Error {
	return &flags.Error{
		Message: fmt.Sprintf("--properties: "+fs, args...),
	}
}

func debugStringer(e *C.struct_daos_prop_entry, name string) string {
	if e == nil {
		return propNotFound(name)
	}

	return fmt.Sprintf("property %q: %+v", name, e)
}

func uintStringer(e *C.struct_daos_prop_entry, name string) string {
	if e == nil {
		return propNotFound(name)
	}

	return fmt.Sprintf("%d", C.get_dpe_str(e))
}

func humanSizeStringer(e *C.struct_daos_prop_entry, name string) string {
	if e == nil {
		return propNotFound(name)
	}

	return humanize.IBytes(uint64(C.get_dpe_val(e)))
}

var hssFn = humanSizeStringer

func strValStringer(e *C.struct_daos_prop_entry, name string) string {
	if e == nil {
		return propNotFound(name)
	}

	cStr := C.get_dpe_str(e)
	if cStr == nil {
		return propNotFound(name)
	}
	return C.GoString(cStr)
}

type propSlice []C.struct_daos_prop_entry

func (ps propSlice) getEntry(pType C.uint32_t) *C.struct_daos_prop_entry {
	for _, entry := range ps {
		if entry.dpe_type == pType {
			return &entry
		}
	}
	return nil
}

func createPropSlice(props *C.daos_prop_t, numProps int) propSlice {
	// Create a Go slice backed by the props array for easier
	// iteration.
	return (*[1 << 30]C.struct_daos_prop_entry)(
		unsafe.Pointer(props.dpp_entries))[:numProps:numProps]
}

func allocProps(numProps int) (props *C.daos_prop_t, entries propSlice, err error) {
	props = C.daos_prop_alloc(C.uint(numProps))
	if props == nil {
		return nil, nil, errors.New("failed to allocate properties list")
	}

	props.dpp_nr = 0
	entries = createPropSlice(props, numProps)

	return
}

func getContainerProperties(hdl C.daos_handle_t, names ...string) (out []*property, cleanup func(), err error) {
	props, entries, err := allocProps(len(names))
	if err != nil {
		return nil, func() {}, err
	}
	cleanup = func() { C.daos_prop_free(props) }

	for _, name := range names {
		var hdlr *propHdlr
		hdlr, err = propHdlrs.get(name)
		if err != nil {
			return
		}
		entries[props.dpp_nr].dpe_type = C.uint(hdlr.dpeType)

		out = append(out, &property{
			entry:       &entries[props.dpp_nr],
			toString:    hdlr.toString,
			Name:        name,
			Description: hdlr.shortDesc,
		})

		props.dpp_nr++
	}

	rc := C.daos_cont_query(hdl, nil, props, nil)
	if err = daosError(rc); err != nil {
		return nil, cleanup, err
	}

	return
}

// PropertiesFlag implements the flags.Unmarshaler and flags.Completer
// interfaces in order to provide a custom flag type for converting
// command-line arguments into a *C.daos_prop_t array suitable for
// creating a container. Use the SetPropertiesFlag type for setting
// properties on an existing container and the GetPropertiesFlag type
// for getting properties on an existing container.
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
	defer func() {
		if err != nil {
			f.Cleanup()
		}
	}()
	var entries propSlice
	f.props, entries, err = allocProps(len(propHdlrs))
	if err != nil {
		return
	}

	for _, propStr := range strings.Split(fv, ",") {
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

		if err = hdlr.execute(&entries[f.props.dpp_nr], value); err != nil {
			return
		}
		entries[f.props.dpp_nr].dpe_type = C.uint(hdlr.dpeType)

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

type GetPropertiesFlag struct {
	PropertiesFlag

	names []string
}

func (f *GetPropertiesFlag) UnmarshalFlag(fv string) error {
	// Accept a list of property names to fetch, if specified,
	// otherwise just fetch all known properties.
	f.names = strings.Split(fv, ",")
	if len(f.names) == 0 || f.names[0] == "all" {
		f.names = propHdlrs.keys()
	}

	for i, name := range f.names {
		f.names[i] = strings.TrimSpace(name)
		if len(name) == 0 {
			return propError("name must not be empty")
		}
		if len(name) > maxNameLen {
			return propError("name too long (%d > %d)",
				len(name), maxNameLen)
		}
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

	for propKey := range propHdlrs {
		if !f.isAllowedProp(propKey) {
			continue
		}

		if strings.HasPrefix(propKey, match) {
			comps = append(comps, flags.Completion{Item: prefix + propKey})
		}
	}

	return
}

type property struct {
	entry       *C.struct_daos_prop_entry
	toString    entryStringer
	Name        string `json:"name"`
	Description string `json:"description,omitempty"`
}

func (p *property) String() string {
	return p.toString(p.entry, p.Name)
}

func (p *property) MarshalJSON() ([]byte, error) {
	if p == nil || p.entry == nil {
		return nil, errors.New("nil property")
	}

	if p.toString == nil {
		p.toString = debugStringer
	}

	// Normal case, just use the stringer.
	jsonValue := func(e *C.struct_daos_prop_entry, n string) interface{} {
		return p.toString(e, n)
	}
	// Special-case situations for when the string representation
	// of a value would be wrong for JSON consumers (e.g. human-readable
	// numeric values that would have to be converted back to a number).
	switch p.entry.dpe_type {
	case C.DAOS_PROP_CO_ALLOCED_OID,
		C.DAOS_PROP_CO_EC_CELL_SZ,
		C.DAOS_PROP_CO_LAYOUT_VER,
		C.DAOS_PROP_CO_SNAPSHOT_MAX,
		C.DAOS_PROP_CO_CSUM_CHUNK_SIZE,
		C.DAOS_PROP_CO_DEDUP_THRESHOLD:
		jsonValue = func(e *C.struct_daos_prop_entry, n string) interface{} {
			return C.get_dpe_val(e)
		}
	case C.DAOS_PROP_CO_ACL:
		jsonValue = func(e *C.struct_daos_prop_entry, n string) interface{} {
			return getAclStrings(e)
		}
	}

	type toJSON property
	return json.Marshal(&struct {
		Value interface{} `json:"value"`
		*toJSON
	}{
		Value:  jsonValue(p.entry, p.Name),
		toJSON: (*toJSON)(p),
	})
}

func printProperties(out io.Writer, header string, props ...*property) {
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
		row[nameTitle] = prop.Description
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
