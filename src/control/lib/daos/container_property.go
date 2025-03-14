//
// (C) Copyright 2021-2023 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"encoding/json"
	"fmt"
	"sort"
	"strconv"
	"strings"
	"unsafe"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
)

/*
#include <daos_prop.h>
#include <daos/common.h>
#include <daos/multihash.h>
#include <daos/compression.h>
#include <daos/cipher.h>
#include <daos/object.h>
#include <daos/cont_props.h>

void
free_ace_list(char **str, size_t str_count)
{
	int i;

	for (i = 0; i < str_count; i++)
		D_FREE(str[i]);
	D_FREE(str);
}

static inline uint64_t
daos_prop_co_status_val(uint32_t status, uint32_t flag, uint32_t ver)
{
	return DAOS_PROP_CO_STATUS_VAL(status, flag, ver);
}

#cgo LDFLAGS: -ldaos_common -lgurt -lcart
*/
import "C"

// ContainerPropType defines a native Go type for the container property types
// defined in the DAOS API.
type ContainerPropType C.uint

const (
	containerPropMin             ContainerPropType = C.DAOS_PROP_CO_MIN
	ContainerPropLabel           ContainerPropType = C.DAOS_PROP_CO_LABEL
	ContainerPropLayout          ContainerPropType = C.DAOS_PROP_CO_LAYOUT_TYPE
	ContainerPropLayoutVersion   ContainerPropType = C.DAOS_PROP_CO_LAYOUT_VER
	ContainerPropChecksumEnabled ContainerPropType = C.DAOS_PROP_CO_CSUM
	ContainerPropChecksumSize    ContainerPropType = C.DAOS_PROP_CO_CSUM_CHUNK_SIZE
	ContainerPropChecksumSrvVrfy ContainerPropType = C.DAOS_PROP_CO_CSUM_SERVER_VERIFY
	ContainerPropRedunFactor     ContainerPropType = C.DAOS_PROP_CO_REDUN_FAC
	ContainerPropRedunLevel      ContainerPropType = C.DAOS_PROP_CO_REDUN_LVL
	ContainerPropMaxSnapshots    ContainerPropType = C.DAOS_PROP_CO_SNAPSHOT_MAX
	ContainerPropACL             ContainerPropType = C.DAOS_PROP_CO_ACL
	ContainerPropCompression     ContainerPropType = C.DAOS_PROP_CO_COMPRESS
	ContainerPropEncrypted       ContainerPropType = C.DAOS_PROP_CO_ENCRYPT
	ContainerPropOwner           ContainerPropType = C.DAOS_PROP_CO_OWNER
	ContainerPropGroup           ContainerPropType = C.DAOS_PROP_CO_OWNER_GROUP
	ContainerPropDedupEnabled    ContainerPropType = C.DAOS_PROP_CO_DEDUP
	ContainerPropDedupThreshold  ContainerPropType = C.DAOS_PROP_CO_DEDUP_THRESHOLD
	ContainerPropRootObjects     ContainerPropType = C.DAOS_PROP_CO_ROOTS
	ContainerPropStatus          ContainerPropType = C.DAOS_PROP_CO_STATUS
	ContainerPropHighestOid      ContainerPropType = C.DAOS_PROP_CO_ALLOCED_OID
	ContainerPropEcCellSize      ContainerPropType = C.DAOS_PROP_CO_EC_CELL_SZ
	ContainerPropEcPerfDom       ContainerPropType = C.DAOS_PROP_CO_EC_PDA
	ContainerPropEcPerfDomAff    ContainerPropType = C.DAOS_PROP_CO_RP_PDA
	ContainerPropGlobalVersion   ContainerPropType = C.DAOS_PROP_CO_GLOBAL_VERSION
	ContainerPropScubberDisabled ContainerPropType = C.DAOS_PROP_CO_SCRUBBER_DISABLED
	ContainerPropObjectVersion   ContainerPropType = C.DAOS_PROP_CO_OBJ_VERSION
	ContainerPropPerfDomain      ContainerPropType = C.DAOS_PROP_CO_PERF_DOMAIN
	containerPropMax             ContainerPropType = C.DAOS_PROP_CO_MAX
)

func (cpt ContainerPropType) String() string {
	switch cpt {
	case ContainerPropLabel:
		return C.DAOS_PROP_ENTRY_LABEL
	case ContainerPropLayout:
		return C.DAOS_PROP_ENTRY_LAYOUT_TYPE
	case ContainerPropLayoutVersion:
		return C.DAOS_PROP_ENTRY_LAYOUT_VER
	case ContainerPropChecksumEnabled:
		return C.DAOS_PROP_ENTRY_CKSUM
	case ContainerPropChecksumSize:
		return C.DAOS_PROP_ENTRY_CKSUM_SIZE
	case ContainerPropChecksumSrvVrfy:
		return C.DAOS_PROP_ENTRY_SRV_CKSUM
	case ContainerPropRedunFactor:
		return C.DAOS_PROP_ENTRY_REDUN_FAC
	case ContainerPropRedunLevel:
		return C.DAOS_PROP_ENTRY_REDUN_LVL
	case ContainerPropMaxSnapshots:
		return C.DAOS_PROP_ENTRY_SNAPSHOT_MAX
	case ContainerPropACL:
		return C.DAOS_PROP_ENTRY_ACL
	case ContainerPropCompression:
		return C.DAOS_PROP_ENTRY_COMPRESS
	case ContainerPropEncrypted:
		return C.DAOS_PROP_ENTRY_ENCRYPT
	case ContainerPropOwner:
		return C.DAOS_PROP_ENTRY_OWNER
	case ContainerPropGroup:
		return C.DAOS_PROP_ENTRY_GROUP
	case ContainerPropDedupEnabled:
		return C.DAOS_PROP_ENTRY_DEDUP
	case ContainerPropDedupThreshold:
		return C.DAOS_PROP_ENTRY_DEDUP_THRESHOLD
	case ContainerPropRootObjects:
		return C.DAOS_PROP_ENTRY_ROOT_OIDS
	case ContainerPropStatus:
		return C.DAOS_PROP_ENTRY_STATUS
	case ContainerPropHighestOid:
		return C.DAOS_PROP_ENTRY_ALLOCED_OID
	case ContainerPropEcCellSize:
		return C.DAOS_PROP_ENTRY_EC_CELL_SZ
	case ContainerPropEcPerfDom:
		return C.DAOS_PROP_ENTRY_EC_PDA
	case ContainerPropEcPerfDomAff:
		return C.DAOS_PROP_ENTRY_RP_PDA
	case ContainerPropGlobalVersion:
		return C.DAOS_PROP_ENTRY_GLOBAL_VERSION
	case ContainerPropScubberDisabled:
		return C.DAOS_PROP_ENTRY_SCRUB_DISABLED
	case ContainerPropObjectVersion:
		return C.DAOS_PROP_ENTRY_OBJ_VERSION
	case ContainerPropPerfDomain:
		return C.DAOS_PROP_ENTRY_PERF_DOMAIN
	default:
		return fmt.Sprintf("unknown container property type %d", cpt)
	}
}

// FromString attempts to resolve the supplied string into a container property
// type.
func (cpt *ContainerPropType) FromString(in string) error {
	// NB: The implication here is that every container property should
	// have an entry in the handler map in order to be resolvable. This
	// should be reasonable. The alternative is a manually-maintained
	// list that is the inverse of String().
	ph, err := propHdlrs.get(in)
	if err != nil {
		return err
	}

	*cpt = ContainerPropType(ph.dpeType)
	return nil
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
//		"key": {                                // used as the --property name
//	     C.DAOS_PROP_ENUM_VAL,               // identify the property type
//	     "short description",                // human-readable (short) description
//	     closure of type entryHdlr,          // process user input (optional for read-only)
//	     map[string]valHdlr{                 // optional map of fixed-string input processors
//	         "key": closure of type valHdlr, // convert fixed-string input to property value
//	     },
//	     []string,                           // optional slice of deprecated keys
//	     closure of type entryStringer,      // optional pretty-printer
//	     bool,                               // if true, property is read-only
//		},
var propHdlrs = propHdlrMap{
	C.DAOS_PROP_ENTRY_LABEL: {
		C.DAOS_PROP_CO_LABEL,
		"Label",
		func(_ *propHdlr, p *ContainerProperty, v string) error {
			if !LabelIsValid(v) {
				return errors.Errorf("invalid label %q", v)
			}
			p.entry.dpe_type = C.DAOS_PROP_CO_LABEL
			p.SetString(v)
			return nil
		},
		nil, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			return p.GetString()
		},
		false,
	},
	C.DAOS_PROP_ENTRY_CKSUM: {
		C.DAOS_PROP_CO_CSUM,
		"Checksum",
		func(h *propHdlr, p *ContainerProperty, v string) error {
			vh, err := h.valHdlrs.get("cksum", v)
			if err != nil {
				return err
			}

			return vh(p, v)
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
		}, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			dpeVal := C.int(p.GetValue())
			if dpeVal == C.DAOS_PROP_CO_CSUM_OFF {
				return "off"
			}

			csum := C.daos_mhash_type2algo(
				C.daos_contprop2hashtype(dpeVal))
			if csum == nil {
				return propInvalidValue(p, name)
			}
			return C.GoString(csum.cf_name)
		},
		false,
	},
	C.DAOS_PROP_ENTRY_CKSUM_SIZE: {
		C.DAOS_PROP_CO_CSUM_CHUNK_SIZE,
		"Checksum Chunk Size",
		func(_ *propHdlr, p *ContainerProperty, v string) error {
			size, err := humanize.ParseBytes(v)
			if err != nil {
				return propError("invalid cksum_size %q (try N<unit>)", v)
			}

			p.SetValue(size)
			return nil
		},
		nil, nil,
		humanSizeStringer,
		false,
	},
	C.DAOS_PROP_ENTRY_SRV_CKSUM: {
		C.DAOS_PROP_CO_CSUM_SERVER_VERIFY,
		"Server Checksumming",
		func(h *propHdlr, p *ContainerProperty, v string) error {
			vh, err := h.valHdlrs.get("srv_cksum", v)
			if err != nil {
				return err
			}

			return vh(p, v)
		},
		valHdlrMap{
			"on":  genSetValHdlr(C.DAOS_PROP_CO_CSUM_SV_ON),
			"off": genSetValHdlr(C.DAOS_PROP_CO_CSUM_SV_OFF),
		}, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			switch p.GetValue() {
			case C.DAOS_PROP_CO_CSUM_SV_OFF:
				return "off"
			case C.DAOS_PROP_CO_CSUM_SV_ON:
				return "on"
			default:
				return propInvalidValue(p, name)
			}
		},
		false,
	},
	C.DAOS_PROP_ENTRY_DEDUP: {
		C.DAOS_PROP_CO_DEDUP,
		"Deduplication",
		func(h *propHdlr, p *ContainerProperty, v string) error {
			vh, err := h.valHdlrs.get("dedup", v)
			if err != nil {
				return err
			}

			return vh(p, v)
		},
		valHdlrMap{
			"off":    genSetValHdlr(C.DAOS_PROP_CO_DEDUP_OFF),
			"memcmp": genSetValHdlr(C.DAOS_PROP_CO_DEDUP_MEMCMP),
			"hash":   genSetValHdlr(C.DAOS_PROP_CO_DEDUP_HASH),
		}, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			switch p.GetValue() {
			case C.DAOS_PROP_CO_DEDUP_OFF:
				return "off"
			case C.DAOS_PROP_CO_DEDUP_MEMCMP:
				return "memcmp"
			case C.DAOS_PROP_CO_DEDUP_HASH:
				return "hash"
			default:
				return propInvalidValue(p, name)
			}
		},
		false,
	},
	C.DAOS_PROP_ENTRY_DEDUP_THRESHOLD: {
		C.DAOS_PROP_CO_DEDUP_THRESHOLD,
		"Dedupe Threshold",
		func(_ *propHdlr, p *ContainerProperty, v string) error {
			size, err := humanize.ParseBytes(v)
			if err != nil {
				return propError("invalid dedup_threshold %q (try N<unit>)", v)
			}

			p.SetValue(size)
			return nil
		},
		nil, nil,
		humanSizeStringer,
		false,
	},
	C.DAOS_PROP_ENTRY_COMPRESS: {
		C.DAOS_PROP_CO_COMPRESS,
		"Compression",
		func(h *propHdlr, p *ContainerProperty, v string) error {
			vh, err := h.valHdlrs.get("compression", v)
			if err != nil {
				return err
			}

			return vh(p, v)
		},
		valHdlrMap{
			"off":      compressHdlr,
			"lz4":      compressHdlr,
			"deflate":  compressHdlr,
			"deflate1": compressHdlr,
			"deflate2": compressHdlr,
			"deflate3": compressHdlr,
			"deflate4": compressHdlr,
		}, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			dpeVal := C.int(p.GetValue())
			if dpeVal == C.DAOS_PROP_CO_CSUM_OFF {
				return "off"
			}

			qatPreferred := C.bool(true)
			algo := C.daos_compress_type2algo(
				C.daos_contprop2compresstype(dpeVal), qatPreferred)
			if algo == nil {
				return propInvalidValue(p, name)
			}
			return C.GoString(algo.cf_name)
		},
		false,
	},
	C.DAOS_PROP_ENTRY_ENCRYPT: {
		C.DAOS_PROP_CO_ENCRYPT,
		"Encryption",
		func(h *propHdlr, p *ContainerProperty, v string) error {
			vh, err := h.valHdlrs.get("encryption", v)
			if err != nil {
				return err
			}

			return vh(p, v)
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
		}, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			dpeVal := C.int(p.GetValue())
			if dpeVal == C.DAOS_PROP_CO_ENCRYPT_OFF {
				return "off"
			}

			algo := C.daos_cipher_type2algo(
				C.daos_contprop2ciphertype(dpeVal))
			if algo == nil {
				return propInvalidValue(p, name)
			}
			return C.GoString(algo.cf_name)
		},
		false,
	},
	C.DAOS_PROP_ENTRY_REDUN_FAC: {
		C.DAOS_PROP_CO_REDUN_FAC,
		"Redundancy Factor",
		func(h *propHdlr, p *ContainerProperty, v string) error {
			vh, err := h.valHdlrs.get("rd_fac", v)
			if err != nil {
				return err
			}

			return vh(p, v)
		},
		valHdlrMap{
			"0": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RF0),
			"1": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RF1),
			"2": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RF2),
			"3": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RF3),
			"4": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RF4),
		}, []string{"rf"},
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			switch p.GetValue() {
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
				return propInvalidValue(p, name)
			}
		},
		false,
	},
	C.DAOS_PROP_ENTRY_STATUS: {
		C.DAOS_PROP_CO_STATUS,
		"Health",
		func(h *propHdlr, p *ContainerProperty, v string) error {
			vh, err := h.valHdlrs.get("status", v)
			if err != nil {
				return err
			}

			return vh(p, v)
		},
		valHdlrMap{
			"healthy": genSetValHdlr(uint64(C.daos_prop_co_status_val(C.DAOS_PROP_CO_HEALTHY, 0, 0))),
		}, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			coInt := C.uint64_t(p.GetValue())
			var coStatus C.struct_daos_co_status

			C.daos_prop_val_2_co_status(coInt, &coStatus)
			switch coStatus.dcs_status {
			case C.DAOS_PROP_CO_HEALTHY:
				return "HEALTHY"
			case C.DAOS_PROP_CO_UNCLEAN:
				return "UNCLEAN"
			default:
				return propInvalidValue(p, name)
			}
		},
		false,
	},
	C.DAOS_PROP_ENTRY_EC_CELL_SZ: {
		C.DAOS_PROP_CO_EC_CELL_SZ,
		"EC Cell Size",
		func(_ *propHdlr, p *ContainerProperty, v string) error {
			size, err := humanize.ParseBytes(v)
			if err != nil {
				return propError("invalid EC cell size %q (try N<unit>)", v)
			}

			if !C.daos_ec_cs_valid(C.uint32_t(size)) {
				return errors.Errorf("invalid EC cell size %d", size)
			}

			p.SetValue(size)
			return nil
		},
		nil, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}

			size := p.GetValue()
			if !C.daos_ec_cs_valid(C.uint32_t(size)) {
				return fmt.Sprintf("invalid size %d", size)
			}
			return humanSizeStringer(p, name)
		},
		false,
	},
	C.DAOS_PROP_ENTRY_EC_PDA: {
		C.DAOS_PROP_CO_EC_PDA,
		"Performance domain affinity level of EC",
		func(_ *propHdlr, p *ContainerProperty, v string) error {
			value, err := strconv.ParseUint(v, 10, 32)
			if err != nil {
				return propError("invalid EC PDA %q", v)
			}

			if !C.daos_ec_pda_valid(C.uint32_t(value)) {
				return errors.Errorf("invalid EC PDA %d", value)
			}

			p.SetValue(value)
			return nil
		},
		nil, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			if p.IsUnset() {
				return "not set"
			}

			value := p.GetValue()
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
		func(_ *propHdlr, p *ContainerProperty, v string) error {
			value, err := strconv.ParseUint(v, 10, 32)
			if err != nil {
				return propError("invalid RP PDA %q", v)
			}

			if !C.daos_rp_pda_valid(C.uint32_t(value)) {
				return errors.Errorf("invalid RP PDA %d", value)
			}

			p.SetValue(value)
			return nil
		},
		nil, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}

			if p.IsUnset() {
				return "not set"
			}
			value := p.GetValue()
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
		nil, nil, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			var loStr [10]C.char
			loInt := C.ushort(p.GetValue())

			C.daos_unparse_ctype(loInt, &loStr[0])
			return fmt.Sprintf("%s (%d)",
				C.GoString((*C.char)(unsafe.Pointer(&loStr[0]))), loInt)
		},
		true,
	},
	C.DAOS_PROP_ENTRY_LAYOUT_VER: {
		C.DAOS_PROP_CO_LAYOUT_VER,
		"Layout Version",
		nil, nil, nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_REDUN_LVL: {
		C.DAOS_PROP_CO_REDUN_LVL,
		"Redundancy Level",
		func(h *propHdlr, p *ContainerProperty, v string) error {
			vh, err := h.valHdlrs.get("rd_lvl", v)
			if err != nil {
				return err
			}

			return vh(p, v)
		},
		valHdlrMap{
			"1":    genSetValHdlr(C.DAOS_PROP_CO_REDUN_RANK),
			"2":    genSetValHdlr(C.DAOS_PROP_CO_REDUN_NODE),
			"rank": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RANK),
			"node": genSetValHdlr(C.DAOS_PROP_CO_REDUN_NODE),
		}, []string{"rf_lvl"},
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}

			lvl := p.GetValue()
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
		func(h *propHdlr, p *ContainerProperty, v string) error {
			vh, err := h.valHdlrs.get("perf_domain", v)
			if err != nil {
				return err
			}

			return vh(p, v)
		},
		valHdlrMap{
			"root":  genSetValHdlr(C.DAOS_PROP_PERF_DOMAIN_ROOT),
			"group": genSetValHdlr(C.DAOS_PROP_PERF_DOMAIN_GROUP),
		}, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}

			lvl := p.GetValue()
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
		nil, nil, nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_ALLOCED_OID: {
		C.DAOS_PROP_CO_ALLOCED_OID,
		"Highest Allocated OID",
		nil, nil, nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_OWNER: {
		C.DAOS_PROP_CO_OWNER,
		"Owner",
		nil, nil, nil,
		strValStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_GROUP: {
		C.DAOS_PROP_CO_OWNER_GROUP,
		"Group",
		nil, nil, nil,
		strValStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_GLOBAL_VERSION: {
		C.DAOS_PROP_CO_GLOBAL_VERSION,
		"Global Version",
		nil, nil, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			if p.IsUnset() {
				return "not set"
			}

			return fmt.Sprintf("%d", p.GetValue())
		},
		true,
	},
	C.DAOS_PROP_ENTRY_OBJ_VERSION: {
		C.DAOS_PROP_CO_OBJ_VERSION,
		"Object Version",
		nil, nil, nil,
		func(p *ContainerProperty, name string) string {
			if p == nil {
				return propNotFound(name)
			}
			if p.IsUnset() {
				return "not set"
			}

			return fmt.Sprintf("%d", p.GetValue())
		},
		true,
	},
	C.DAOS_PROP_ENTRY_ACL: {
		C.DAOS_PROP_CO_ACL,
		"Access Control List",
		nil, nil, nil,
		aclStringer,
		true,
	},
}

// NB: Most feature work should not require modification to the code
// below.

type (
	ContainerProperty struct {
		property
		hdlr        *propHdlr
		Type        ContainerPropType `json:"-"`
		Name        string            `json:"name"`
		Description string            `json:"description"`
	}

	ContainerPropertyList struct {
		propertyList
	}
)

func AllocateContainerPropertyList(count int) (*ContainerPropertyList, error) {
	// Special case: If count is zero, allocate room for all properties.
	if count == 0 {
		count = len(propHdlrs)
	}

	pl, err := newPropertyList(count)
	if err != nil {
		return nil, err
	}

	return &ContainerPropertyList{
		propertyList: *pl,
	}, nil
}

func NewContainerPropertyList(propNames ...string) (*ContainerPropertyList, error) {
	// Special case: If no property names are requested, retrieve all.
	if len(propNames) == 0 {
		propNames = propHdlrs.keys()
	}

	propList, err := AllocateContainerPropertyList(len(propNames))
	if err != nil {
		return nil, err
	}

	for _, name := range propNames {
		if _, err := propList.AddEntryByName(name); err != nil {
			return nil, err
		}
	}

	return propList, nil
}

func (cpl *ContainerPropertyList) CopyFrom(other *ContainerPropertyList) error {
	if other == nil {
		return errors.New("nil source property list")
	}
	availEntries := len(cpl.entries) - int(cpl.cProps.dpp_nr)
	if availEntries < int(other.cProps.dpp_nr) {
		return errors.Errorf("destination property list does not have enough room (%d < %d)", availEntries, other.cProps.dpp_nr)
	}

	for oi := 0; oi < int(other.cProps.dpp_nr); oi++ {
		if rc := C.daos_prop_entry_copy(&other.entries[oi], &cpl.entries[cpl.cProps.dpp_nr]); rc != 0 {
			return Status(rc)
		}
		cpl.cProps.dpp_nr++
	}

	return nil
}

func (cpl *ContainerPropertyList) MustAddEntryByType(propType ContainerPropType) *ContainerProperty {
	prop, err := cpl.AddEntryByType(propType)
	if err != nil {
		panic(err)
	}
	return prop
}

func (cpl *ContainerPropertyList) DelEntryByType(propType ContainerPropType) error {
	if cpl.immutable {
		return ErrPropertyListImmutable
	}

	// KISS for now.
	if len(cpl.entries) < 2 {
		return errors.Errorf("cannot delete entry from list of size %d", len(cpl.entries))
	}

	var hasEntry bool
	for _, prop := range cpl.Properties() {
		if prop.Type == propType {
			hasEntry = true
			break
		}
	}
	if !hasEntry {
		return errors.Errorf("property %q not found in list", propType)
	}

	newPl, err := newPropertyList(len(cpl.entries) - 1)
	if err != nil {
		return err
	}

	for _, e := range cpl.entries {
		if ContainerPropType(e.dpe_type) != propType {
			newPl.entries[newPl.cProps.dpp_nr].dpe_type = e.dpe_type
			newPl.cProps.dpp_nr++
		}
	}
	cpl.Free()
	cpl.propertyList = *newPl

	return nil
}

func (cpl *ContainerPropertyList) AddEntryByType(propType ContainerPropType) (*ContainerProperty, error) {
	if cpl.immutable {
		return nil, ErrPropertyListImmutable
	}

	if propType < containerPropMin || propType > containerPropMax {
		return nil, errors.Wrapf(InvalidInput, "invalid container property type %d", propType)
	}

	if int(cpl.cProps.dpp_nr) == len(cpl.entries) {
		return nil, errors.Errorf("property list is full (%d/%d entries)", len(cpl.entries), cap(cpl.entries))
	}

	cpl.entries[cpl.cProps.dpp_nr].dpe_type = C.uint(propType)
	prop := newContainerProperty(int(cpl.cProps.dpp_nr), &cpl.entries[cpl.cProps.dpp_nr])
	cpl.cProps.dpp_nr++

	return prop, nil
}

func (cpl *ContainerPropertyList) MustAddEntryByName(name string) *ContainerProperty {
	prop, err := cpl.AddEntryByName(name)
	if err != nil {
		panic(err)
	}
	return prop
}

func (cpl *ContainerPropertyList) AddEntryByName(name string) (*ContainerProperty, error) {
	if cpl.immutable {
		return nil, ErrPropertyListImmutable
	}

	key := strings.TrimSpace(name)
	if len(key) == 0 {
		return nil, propError("name must not be empty")
	}
	if len(key) > maxNameLen {
		return nil, propError("%q: name too long (%d > %d)", key, len(key), maxNameLen)
	}

	propType := ContainerPropType(0)
	if err := propType.FromString(key); err != nil {
		return nil, err
	}

	return cpl.AddEntryByType(propType)
}

func newContainerProperty(listIdx int, entry *C.struct_daos_prop_entry) *ContainerProperty {
	name := ContainerPropType(entry.dpe_type).String()
	hdlr := propHdlrs[name]
	if hdlr == nil {
		hdlr = &propHdlr{
			dpeType:   C.enum_daos_cont_props(entry.dpe_type),
			shortDesc: name + " (DEBUG)",
			readOnly:  true,
			toString:  debugStringer,
		}
	}

	return &ContainerProperty{
		property: property{
			idx:   C.int(listIdx),
			entry: entry,
		},
		hdlr:        hdlr,
		Type:        ContainerPropType(hdlr.dpeType),
		Name:        name,
		Description: hdlr.shortDesc,
	}
}

func (cpl *ContainerPropertyList) PropertyKeys(incReadOnly bool) (keys []string) {
	keys = make([]string, 0, len(cpl.entries))
	for _, prop := range cpl.Properties() {
		if prop.IsReadOnly() && !incReadOnly {
			continue
		}

		keys = append(keys, prop.Name)
		if prop.hdlr.deprecatedKeys != nil {
			keys = append(keys, prop.hdlr.deprecatedKeys...)
		}
	}
	return
}

func (cpl *ContainerPropertyList) Properties() (props []*ContainerProperty) {
	for i := range cpl.entries {
		props = append(props, newContainerProperty(i, &cpl.entries[i]))
	}
	return
}

func (cpl *ContainerPropertyList) SetPropertyByType(propType ContainerPropType, value string) error {
	var prop *ContainerProperty
	for _, p := range cpl.Properties() {
		if p.Type == propType {
			prop = p
			break
		}
	}

	if prop == nil {
		return errors.Errorf("property %q not found in list", propType)
	}

	return prop.Set(value)
}

func (cpl *ContainerPropertyList) SetPropertyByName(name, value string) error {
	var propType ContainerPropType
	if err := propType.FromString(name); err != nil {
		return err
	}

	return cpl.SetPropertyByType(propType, value)
}

func (cp *ContainerProperty) IsReadOnly() bool {
	return cp.hdlr.readOnly
}

func (cp *ContainerProperty) Set(value string) error {
	if cp.IsReadOnly() {
		return errors.Errorf("property %q is read-only", cp.Name)
	}

	return cp.hdlr.execute(cp, value)
}

func (cp *ContainerProperty) StringValue() string {
	valStr := cp.property.String()
	if cp.hdlr != nil && cp.hdlr.toString != nil {
		valStr = cp.hdlr.toString(cp, cp.Name)
	}
	return valStr
}

func (cp *ContainerProperty) String() string {
	return fmt.Sprintf("%s: %s", cp.Name, cp.StringValue())
}

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
	// deprecatedKeys defines a list of deprecated keys for
	// the property. Optional.
	deprecatedKeys []string
	// toString defines a closure for converting the
	// entry's value into a string.
	toString entryStringer
	// readOnly indicates that the property may not be set.
	readOnly bool
}

const (
	maxNameLen  = 20 // arbitrary; came from C code
	maxValueLen = C.DAOS_PROP_LABEL_MAX_LEN
)

type entryHdlr func(*propHdlr, *ContainerProperty, string) error
type valHdlr func(*ContainerProperty, string) error
type entryStringer func(*ContainerProperty, string) string

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

func (ph *propHdlr) execute(p *ContainerProperty, v string) error {
	if ph.nameHdlr == nil {
		return propError("no name handler set")
	}

	return ph.nameHdlr(ph, p, v)
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
	searchKey := strings.ToLower(strings.TrimSpace(prop))
	ph, found := phm[searchKey]
	if !found {
		// fall back to slow search for a deprecated key
		for _, ph := range phm {
			if ph.deprecatedKeys != nil {
				for _, key := range ph.deprecatedKeys {
					if key == searchKey {
						return ph, nil
					}
				}
			}
		}
		return nil, propError("unknown property %q (valid: %s)", prop, strings.Join(phm.keys(), ","))
	}
	return ph, nil
}

func cksumHdlr(p *ContainerProperty, v string) error {
	csumVal := C.CString(v)
	defer freeString(csumVal)
	csumType := C.daos_str2csumcontprop(csumVal)

	if csumType < 0 {
		return propError("unknown checksum type %q", v)
	}

	p.SetValue(uint64(csumType))
	return nil
}

func compressHdlr(p *ContainerProperty, v string) error {
	compVal := C.CString(v)
	defer freeString(compVal)
	compType := C.daos_str2compresscontprop(compVal)

	if compType < 0 {
		return propError("unknown compression type %q", v)
	}

	p.SetValue(uint64(compType))
	return nil
}

func encryptHdlr(p *ContainerProperty, v string) error {
	encVal := C.CString(v)
	defer freeString(encVal)
	encType := C.daos_str2encryptcontprop(encVal)

	if encType < 0 {
		return propError("unknown encryption type %q", v)
	}

	p.SetValue(uint64(encType))
	return nil
}

func genSetValHdlr(v uint64) valHdlr {
	return func(p *ContainerProperty, _ string) error {
		p.SetValue(v)
		return nil
	}
}

func propNotFound(name string) string {
	return fmt.Sprintf("property %q not found", name)
}

func propInvalidValue(p *ContainerProperty, name string) string {
	return fmt.Sprintf("property %q: invalid value %x",
		name, p.GetValue())
}

func propError(fs string, args ...interface{}) error {
	return errors.Errorf("properties: "+fs, args...)
}

func debugStringer(p *ContainerProperty, name string) string {
	if p == nil {
		return propNotFound(name)
	}

	return fmt.Sprintf("property %q: %+v", name, p.entry)
}

func uintStringer(p *ContainerProperty, name string) string {
	if p == nil {
		return propNotFound(name)
	}

	return fmt.Sprintf("%d", p.GetValue())
}

func humanSizeStringer(p *ContainerProperty, name string) string {
	if p == nil {
		return propNotFound(name)
	}

	return humanize.IBytes(p.GetValue())
}

var hssFn = humanSizeStringer

func strValStringer(p *ContainerProperty, name string) string {
	if p == nil {
		return propNotFound(name)
	}

	str := p.GetString()
	if str == "" {
		return propNotFound(name)
	}
	return str
}

func (p *ContainerProperty) SettableValues() []string {
	if p.hdlr == nil || p.hdlr.valHdlrs == nil {
		return nil
	}

	return p.hdlr.valHdlrs.keys()
}

func (p *ContainerProperty) MarshalJSON() ([]byte, error) {
	if p == nil || p.entry == nil || p.hdlr == nil {
		return nil, errors.New("nil property")
	}

	if p.hdlr.toString == nil {
		p.hdlr.toString = debugStringer
	}

	// Normal case, just use the stringer.
	jsonValue := func(p *ContainerProperty, n string) interface{} {
		return p.hdlr.toString(p, n)
	}
	// Special-case situations for when the string representation
	// of a value would be wrong for JSON consumers (e.g. human-readable
	// numeric values that would have to be converted back to a number).
	switch p.Type {
	case ContainerPropHighestOid,
		ContainerPropEcCellSize,
		ContainerPropLayoutVersion,
		ContainerPropMaxSnapshots,
		ContainerPropChecksumSize,
		ContainerPropDedupThreshold:
		jsonValue = func(p *ContainerProperty, n string) interface{} {
			return p.GetValue()
		}
	case ContainerPropACL:
		jsonValue = func(p *ContainerProperty, n string) interface{} {
			return getAclStrings(p)
		}
	}

	type toJSON ContainerProperty
	return json.Marshal(&struct {
		Value interface{} `json:"value"`
		*toJSON
	}{
		Value:  jsonValue(p, p.Name),
		toJSON: (*toJSON)(p),
	})
}

func getAclStrings(p *ContainerProperty) (out []string) {
	acl := (*C.struct_daos_acl)(p.GetValuePtr())
	if acl == nil {
		return
	}

	var aces **C.char
	var acesNr C.size_t

	rc := C.daos_acl_to_strs(acl, &aces, &acesNr)
	if rc != 0 || aces == nil {
		return
	}
	defer C.free_ace_list(aces, acesNr)

	for _, ace := range (*[1 << 30]*C.char)(unsafe.Pointer(aces))[:acesNr:acesNr] {
		out = append(out, C.GoString(ace))
	}

	return
}

func aclStringer(p *ContainerProperty, name string) string {
	if p == nil {
		return propNotFound(name)
	}

	return strings.Join(getAclStrings(p), ", ")
}
