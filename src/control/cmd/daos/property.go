//
// (C) Copyright 2021-2023 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

/*
#include "util.h"

#include <daos/multihash.h>
#include <daos/compression.h>
#include <daos/cipher.h>
#include <daos/object.h>
#include <daos/cont_props.h>
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
	// readOnly indicates that the property may not be set.
	readOnly bool
}

// newTestPropEntry returns an initialized property entry for testing.
// NB: The entry is initialized with Go-managed memory, so it is not
// suitable for use when calling real C functions.
func newTestPropEntry() *C.struct_daos_prop_entry {
	return new(C.struct_daos_prop_entry)
}

// getDpeVal returns the value of the given property entry.
func getDpeVal(e *C.struct_daos_prop_entry) (uint64, error) {
	if e == nil {
		return 0, errors.New("nil property entry")
	}

	v := C.get_dpe_val(e)
	return uint64(v), nil
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
//
//	"key": {		           // used as the --property name
//		C.DAOS_PROP_ENUM_VAL,      // identify the property type
//		"short description",       // human-readable (short) description
//		closure of type entryHdlr, // process set entry (optional for read-only)
//		map[string]valHdlr{	   // optional map of string value processors
//			"key": closure of type valHdlr, // process set value
//		},
//		closure of type entryStringer, // optional pretty-printer
//		bool,			   // if true, property may not be set
//	},
var propHdlrs = propHdlrMap{
	C.DAOS_PROP_ENTRY_LABEL: {
		C.DAOS_PROP_CO_LABEL,
		"Label",
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			if !daos.LabelIsValid(v) {
				return errors.Errorf("invalid label %q", v)
			}
			e.dpe_type = C.DAOS_PROP_CO_LABEL
			cStr := C.CString(v)
			defer freeString(cStr)
			rc := C.daos_prop_entry_set_str(e, cStr, C.strlen(cStr))
			if err := daosError(rc); err != nil {
				return err
			}
			return nil
		},
		nil,
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			if C.get_dpe_str(e) == nil {
				return ""
			}
			return strValStringer(e, name)
		},
		false,
	},
	C.DAOS_PROP_ENTRY_CKSUM: {
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
		false,
	},
	C.DAOS_PROP_ENTRY_CKSUM_SIZE: {
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
		false,
	},
	C.DAOS_PROP_ENTRY_SRV_CKSUM: {
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
		false,
	},
	C.DAOS_PROP_ENTRY_DEDUP: {
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
		false,
	},
	C.DAOS_PROP_ENTRY_DEDUP_THRESHOLD: {
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
		false,
	},
	C.DAOS_PROP_ENTRY_COMPRESS: {
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
		false,
	},
	C.DAOS_PROP_ENTRY_ENCRYPT: {
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
		false,
	},
	C.DAOS_PROP_ENTRY_REDUN_FAC: {
		C.DAOS_PROP_CO_REDUN_FAC,
		"Redundancy Factor",
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("rd_fac", v)
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
				return "rd_fac0"
			case C.DAOS_PROP_CO_REDUN_RF1:
				return "rd_fac1"
			case C.DAOS_PROP_CO_REDUN_RF2:
				return "rd_fac2"
			case C.DAOS_PROP_CO_REDUN_RF3:
				return "rd_fac3"
			case C.DAOS_PROP_CO_REDUN_RF4:
				return "rd_fac4"
			default:
				return propInvalidValue(e, name)
			}
		},
		false,
	},
	C.DAOS_PROP_ENTRY_STATUS: {
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
		false,
	},
	C.DAOS_PROP_ENTRY_EC_CELL_SZ: {
		C.DAOS_PROP_CO_EC_CELL_SZ,
		"EC Cell Size",
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			size, err := humanize.ParseBytes(v)
			if err != nil {
				return propError("invalid EC cell size %q (try N<unit>)", v)
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
		false,
	},
	C.DAOS_PROP_ENTRY_EC_PDA: {
		C.DAOS_PROP_CO_EC_PDA,
		"Performance domain affinity level of EC",
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			value, err := strconv.ParseUint(v, 10, 32)
			if err != nil {
				return propError("invalid EC PDA %q", v)
			}

			if !C.daos_ec_pda_valid(C.uint32_t(value)) {
				return errors.Errorf("invalid EC PDA %d", value)
			}

			C.set_dpe_val(e, C.uint64_t(value))
			return nil
		},
		nil,
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			if C.dpe_is_negative(e) {
				return fmt.Sprintf("not set")
			}

			value := C.get_dpe_val(e)
			if !C.daos_ec_pda_valid(C.uint32_t(value)) {
				return fmt.Sprintf("invalid ec pda %d", value)
			}
			return fmt.Sprintf("%d", value)
		},
		false,
	},
	C.DAOS_PROP_ENTRY_RP_PDA: {
		C.DAOS_PROP_CO_RP_PDA,
		"Performance domain affinity level of RP",
		func(_ *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			value, err := strconv.ParseUint(v, 10, 32)
			if err != nil {
				return propError("invalid RP PDA %q", v)
			}

			if !C.daos_rp_pda_valid(C.uint32_t(value)) {
				return errors.Errorf("invalid RP PDA %d", value)
			}

			C.set_dpe_val(e, C.uint64_t(value))
			return nil
		},
		nil,
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}

			if C.dpe_is_negative(e) {
				return fmt.Sprintf("not set")
			}
			value := C.get_dpe_val(e)
			if !C.daos_rp_pda_valid(C.uint32_t(value)) {
				return fmt.Sprintf("invalid RP PDA %d", value)
			}
			return fmt.Sprintf("%d", value)
		},
		false,
	},
	// Read-only properties here for use by get-property.
	C.DAOS_PROP_ENTRY_LAYOUT_TYPE: {
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
		true,
	},
	C.DAOS_PROP_ENTRY_LAYOUT_VER: {
		C.DAOS_PROP_CO_LAYOUT_VER,
		"Layout Version",
		nil, nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_REDUN_LVL: {
		C.DAOS_PROP_CO_REDUN_LVL,
		"Redundancy Level",
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("rd_lvl", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"1":    setDpeVal(C.DAOS_PROP_CO_REDUN_RANK),
			"2":    setDpeVal(C.DAOS_PROP_CO_REDUN_NODE),
			"rank": setDpeVal(C.DAOS_PROP_CO_REDUN_RANK),
			"node": setDpeVal(C.DAOS_PROP_CO_REDUN_NODE),
		},
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}

			lvl := C.get_dpe_val(e)
			switch lvl {
			case C.DAOS_PROP_CO_REDUN_RANK:
				return fmt.Sprintf("rank (%d)", lvl)
			case C.DAOS_PROP_CO_REDUN_NODE:
				return fmt.Sprintf("node (%d)", lvl)
			default:
				return fmt.Sprintf("(%d)", lvl)
			}
		},
		false,
	},
	C.DAOS_PROP_ENTRY_PERF_DOMAIN: {
		C.DAOS_PROP_CO_PERF_DOMAIN,
		"Performance domain level",
		func(h *propHdlr, e *C.struct_daos_prop_entry, v string) error {
			vh, err := h.valHdlrs.get("perf_domain", v)
			if err != nil {
				return err
			}

			return vh(e, v)
		},
		valHdlrMap{
			"root":  setDpeVal(C.DAOS_PROP_PERF_DOMAIN_ROOT),
			"group": setDpeVal(C.DAOS_PROP_PERF_DOMAIN_GROUP),
		},
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}

			lvl := C.get_dpe_val(e)
			switch lvl {
			case C.DAOS_PROP_PERF_DOMAIN_ROOT:
				return fmt.Sprintf("root (%d)", lvl)
			case C.DAOS_PROP_PERF_DOMAIN_GROUP:
				return fmt.Sprintf("group (%d)", lvl)
			default:
				return fmt.Sprintf("(%d)", lvl)
			}
		},
		false,
	},
	C.DAOS_PROP_ENTRY_SNAPSHOT_MAX: {
		C.DAOS_PROP_CO_SNAPSHOT_MAX,
		"Max Snapshot",
		nil, nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_ALLOCED_OID: {
		C.DAOS_PROP_CO_ALLOCED_OID,
		"Highest Allocated OID",
		nil, nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_OWNER: {
		C.DAOS_PROP_CO_OWNER,
		"Owner",
		nil, nil,
		strValStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_GROUP: {
		C.DAOS_PROP_CO_OWNER_GROUP,
		"Group",
		nil, nil,
		strValStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_GLOBAL_VERSION: {
		C.DAOS_PROP_CO_GLOBAL_VERSION,
		"Global Version",
		nil, nil,
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			if C.dpe_is_negative(e) {
				return fmt.Sprintf("not set")
			}

			value := C.get_dpe_val(e)
			return fmt.Sprintf("%d", value)
		},
		true,
	},
	C.DAOS_PROP_ENTRY_OBJ_VERSION: {
		C.DAOS_PROP_CO_OBJ_VERSION,
		"Object Version",
		nil, nil,
		func(e *C.struct_daos_prop_entry, name string) string {
			if e == nil {
				return propNotFound(name)
			}
			if C.dpe_is_negative(e) {
				return fmt.Sprintf("not set")
			}

			value := C.get_dpe_val(e)
			return fmt.Sprintf("%d", value)
		},
		true,
	},
}

var contDeprProps = map[string]string{
	"rf":     "rd_fac",
	"rf_lvl": "rd_lvl",
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
		Message: fmt.Sprintf("properties: "+fs, args...),
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
		return nil, nil, errors.Wrap(daos.NoMemory, "failed to allocate properties list")
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
	ui.SetPropertiesFlag

	props *C.daos_prop_t
}

func (f *PropertiesFlag) Complete(match string) []flags.Completion {
	comps := make(ui.CompletionMap)
	for key, hdlr := range propHdlrs {
		if !f.IsSettable(key) {
			continue
		}
		comps[key] = hdlr.valHdlrs.keys()
	}
	f.SetCompletions(comps)

	return f.SetPropertiesFlag.Complete(match)
}

func (f *PropertiesFlag) AddPropVal(key, val string) error {
	var entries propSlice
	var err error
	if f.props == nil {
		f.props, entries, err = allocProps(len(propHdlrs))
		if err != nil {
			return err
		}
	} else {
		entries = createPropSlice(f.props, len(propHdlrs))
	}

	hdlr, err := propHdlrs.get(key)
	if err != nil {
		return err
	}

	for i := 0; i < int(f.props.dpp_nr); i++ {
		if uint32(entries[i].dpe_type) == hdlr.dpeType {
			return errors.Errorf("cannot update value for existing prop %s", hdlr.shortDesc)
		}
	}

	if err := hdlr.execute(&entries[f.props.dpp_nr], val); err != nil {
		return err
	}
	entries[f.props.dpp_nr].dpe_type = C.uint32_t(hdlr.dpeType)
	f.props.dpp_nr++

	if f.ParsedProps == nil {
		f.ParsedProps = make(map[string]string)
	}
	f.ParsedProps[key] = val

	return nil
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

	if err := f.SetPropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	for key, val := range f.ParsedProps {
		var hdlr *propHdlr
		hdlr, err = propHdlrs.get(key)
		if err != nil {
			return
		}

		if err = hdlr.execute(&entries[f.props.dpp_nr], val); err != nil {
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

	C.daos_prop_free(f.props)
}

// CreatePropertiesFlag embeds the base PropertiesFlag struct to
// compose a flag that is used for setting properties on a
// new container. It is intended to be used where only a subset of
// properties are valid for setting on create.
type CreatePropertiesFlag struct {
	PropertiesFlag
}

func (f *CreatePropertiesFlag) setWritableKeys() {
	keys := make([]string, 0, len(propHdlrs))
	for key, hdlr := range propHdlrs {
		if !hdlr.readOnly {
			keys = append(keys, key)
		}
	}
	f.SettableKeys(keys...)
	f.DeprecatedKeyMap(contDeprProps)
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
		f.names = propHdlrs.keys()
	}

	for i, name := range f.names {
		key := strings.TrimSpace(name)
		if len(key) == 0 {
			return propError("name must not be empty")
		}
		if len(key) > maxNameLen {
			return propError("%q: name too long (%d > %d)",
				key, len(key), maxNameLen)
		}
		if newKey, found := contDeprProps[key]; found {
			key = newKey
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

	for propKey := range propHdlrs {
		if !f.IsSettable(propKey) {
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
