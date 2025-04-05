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
	"slices"
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
	ContainerPropLayoutType      ContainerPropType = C.DAOS_PROP_CO_LAYOUT_TYPE
	ContainerPropLayoutVersion   ContainerPropType = C.DAOS_PROP_CO_LAYOUT_VER
	ContainerPropChecksumEnabled ContainerPropType = C.DAOS_PROP_CO_CSUM
	ContainerPropChecksumSize    ContainerPropType = C.DAOS_PROP_CO_CSUM_CHUNK_SIZE
	ContainerPropChecksumSrvVrfy ContainerPropType = C.DAOS_PROP_CO_CSUM_SERVER_VERIFY
	ContainerPropRedunFactor     ContainerPropType = C.DAOS_PROP_CO_REDUN_FAC
	ContainerPropRedunLevel      ContainerPropType = C.DAOS_PROP_CO_REDUN_LVL
	ContainerPropMaxSnapshots    ContainerPropType = C.DAOS_PROP_CO_SNAPSHOT_MAX
	ContainerPropACL             ContainerPropType = C.DAOS_PROP_CO_ACL
	ContainerPropCompression     ContainerPropType = C.DAOS_PROP_CO_COMPRESS
	ContainerPropEncryption      ContainerPropType = C.DAOS_PROP_CO_ENCRYPT
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
	case ContainerPropLayoutType:
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
	case ContainerPropEncryption:
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
// should only need to modify this map. If your new property has a
// custom input processor or pretty-printer, please add a unit test
// for that code!
//
// The structure of an entry is as follows:
//
//	"key": {                                    // used as the --property name
//	        C.DAOS_PROP_ENUM_VAL,               // identify the property type
//	        "short description",                // human-readable (short) description
//	        closure of type entryHdlr,          // process user input (optional for read-only or mapped inputs)
//	        map[string]valHdlr{                 // optional map of fixed-string input processors
//	            "key": closure of type valHdlr, // convert fixed-string input to property value
//	        },
//	        []string,                           // optional slice of deprecated keys
//	        closure of type entryStringer,      // optional pretty-printer
//	        bool,                               // if true, property is read-only
//	},
var propHdlrs = propHdlrMap{
	C.DAOS_PROP_ENTRY_LABEL: {
		C.DAOS_PROP_CO_LABEL,
		"Label",
		func(_ *propHdlr, p *ContainerProperty, v string) error {
			if !LabelIsValid(v) {
				return errors.Errorf("invalid %s %q", p.Name, v)
			}
			p.entry.dpe_type = C.DAOS_PROP_CO_LABEL
			return p.SetString(v)
		},
		nil,
		nil,
		strValStringer,
		false,
	},
	C.DAOS_PROP_ENTRY_CKSUM: {
		C.DAOS_PROP_CO_CSUM,
		"Checksum",
		nil,
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
		nil,
		func(p *ContainerProperty) string {
			dpeVal := C.int(p.GetValue())
			if dpeVal == C.DAOS_PROP_CO_CSUM_OFF {
				return "off"
			}

			csum := C.daos_mhash_type2algo(
				C.daos_contprop2hashtype(dpeVal))
			if csum == nil {
				return propInvalidValue(p)
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
				return propError("invalid %s %q (try N<unit>)", p.Name, v)
			}

			return p.SetValue(size)
		},
		nil,
		nil,
		humanSizeStringer,
		false,
	},
	C.DAOS_PROP_ENTRY_SRV_CKSUM: {
		C.DAOS_PROP_CO_CSUM_SERVER_VERIFY,
		"Server Checksumming",
		nil,
		valHdlrMap{
			"on":  genSetValHdlr(C.DAOS_PROP_CO_CSUM_SV_ON),
			"off": genSetValHdlr(C.DAOS_PROP_CO_CSUM_SV_OFF),
		},
		nil,
		func(p *ContainerProperty) string {
			switch p.GetValue() {
			case C.DAOS_PROP_CO_CSUM_SV_OFF:
				return "off"
			case C.DAOS_PROP_CO_CSUM_SV_ON:
				return "on"
			default:
				return propInvalidValue(p)
			}
		},
		false,
	},
	C.DAOS_PROP_ENTRY_DEDUP: {
		C.DAOS_PROP_CO_DEDUP,
		"Deduplication",
		nil,
		valHdlrMap{
			"off":    genSetValHdlr(C.DAOS_PROP_CO_DEDUP_OFF),
			"memcmp": genSetValHdlr(C.DAOS_PROP_CO_DEDUP_MEMCMP),
			"hash":   genSetValHdlr(C.DAOS_PROP_CO_DEDUP_HASH),
		},
		nil,
		func(p *ContainerProperty) string {
			switch p.GetValue() {
			case C.DAOS_PROP_CO_DEDUP_OFF:
				return "off"
			case C.DAOS_PROP_CO_DEDUP_MEMCMP:
				return "memcmp"
			case C.DAOS_PROP_CO_DEDUP_HASH:
				return "hash"
			default:
				return propInvalidValue(p)
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

			return p.SetValue(size)
		},
		nil,
		nil,
		humanSizeStringer,
		false,
	},
	C.DAOS_PROP_ENTRY_COMPRESS: {
		C.DAOS_PROP_CO_COMPRESS,
		"Compression",
		nil,
		valHdlrMap{
			"off":      compressHdlr,
			"lz4":      compressHdlr,
			"deflate":  compressHdlr,
			"deflate1": compressHdlr,
			"deflate2": compressHdlr,
			"deflate3": compressHdlr,
			"deflate4": compressHdlr,
		},
		nil,
		func(p *ContainerProperty) string {
			dpeVal := C.int(p.GetValue())
			if dpeVal == C.DAOS_PROP_CO_CSUM_OFF {
				return "off"
			}

			qatPreferred := C.bool(true)
			algo := C.daos_compress_type2algo(
				C.daos_contprop2compresstype(dpeVal), qatPreferred)
			if algo == nil {
				return propInvalidValue(p)
			}
			return C.GoString(algo.cf_name)
		},
		false,
	},
	C.DAOS_PROP_ENTRY_ENCRYPT: {
		C.DAOS_PROP_CO_ENCRYPT,
		"Encryption",
		nil,
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
		nil,
		func(p *ContainerProperty) string {
			dpeVal := C.int(p.GetValue())
			if dpeVal == C.DAOS_PROP_CO_ENCRYPT_OFF {
				return "off"
			}

			algo := C.daos_cipher_type2algo(
				C.daos_contprop2ciphertype(dpeVal))
			if algo == nil {
				return propInvalidValue(p)
			}
			return C.GoString(algo.cf_name)
		},
		false,
	},
	C.DAOS_PROP_ENTRY_REDUN_FAC: {
		C.DAOS_PROP_CO_REDUN_FAC,
		"Redundancy Factor",
		nil,
		valHdlrMap{
			"0": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RF0),
			"1": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RF1),
			"2": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RF2),
			"3": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RF3),
			"4": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RF4),
		},
		[]string{"rf"},
		func(p *ContainerProperty) string {
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
				return propInvalidValue(p)
			}
		},
		false,
	},
	C.DAOS_PROP_ENTRY_STATUS: {
		C.DAOS_PROP_CO_STATUS,
		"Health",
		nil,
		valHdlrMap{
			"healthy": genSetValHdlr(uint64(C.daos_prop_co_status_val(C.DAOS_PROP_CO_HEALTHY, 0, 0))),
		},
		nil,
		func(p *ContainerProperty) string {
			coInt := C.uint64_t(p.GetValue())
			var coStatus C.struct_daos_co_status

			C.daos_prop_val_2_co_status(coInt, &coStatus)
			switch coStatus.dcs_status {
			case C.DAOS_PROP_CO_HEALTHY:
				return "HEALTHY"
			case C.DAOS_PROP_CO_UNCLEAN:
				return "UNCLEAN"
			default:
				return propInvalidValue(p)
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
				return propError("invalid %s %q (try N<unit>)", p.Name, v)
			}

			if !EcCellSizeIsValid(size) {
				return errors.Errorf("invalid %s %d", p.Name, size)
			}

			return p.SetValue(size)
		},
		nil,
		nil,
		func(p *ContainerProperty) string {
			size := p.GetValue()
			if !EcCellSizeIsValid(size) {
				return fmt.Sprintf("invalid size %d", size)
			}
			return humanSizeStringer(p)
		},
		false,
	},
	C.DAOS_PROP_ENTRY_EC_PDA: {
		C.DAOS_PROP_CO_EC_PDA,
		"Performance domain affinity level of EC",
		func(_ *propHdlr, p *ContainerProperty, v string) error {
			value, err := strconv.ParseUint(v, 10, 32)
			if err != nil {
				return propError("invalid %s %q", p.Name, v)
			}

			if !EcPdaIsValid(value) {
				return errors.Errorf("invalid %s %d", p.Name, value)
			}

			return p.SetValue(value)
		},
		nil,
		nil,
		func(p *ContainerProperty) string {
			value := p.GetValue()
			if !EcPdaIsValid(value) {
				return fmt.Sprintf("invalid %s %d", p.Name, value)
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
				return propError("invalid %s %q", p.Name, v)
			}

			if !RpPdaIsValid(value) {
				return errors.Errorf("invalid %s %d", p.Name, value)
			}

			return p.SetValue(value)
		},
		nil,
		nil,
		func(p *ContainerProperty) string {
			value := p.GetValue()
			if !RpPdaIsValid(value) {
				return fmt.Sprintf("invalid %s %d", p.Name, value)
			}
			return fmt.Sprintf("%d", value)
		},
		false,
	},
	C.DAOS_PROP_ENTRY_REDUN_LVL: {
		C.DAOS_PROP_CO_REDUN_LVL,
		"Redundancy Level",
		nil,
		valHdlrMap{
			"1":    genSetValHdlr(C.DAOS_PROP_CO_REDUN_RANK),
			"2":    genSetValHdlr(C.DAOS_PROP_CO_REDUN_NODE),
			"rank": genSetValHdlr(C.DAOS_PROP_CO_REDUN_RANK),
			"node": genSetValHdlr(C.DAOS_PROP_CO_REDUN_NODE),
		},
		[]string{"rf_lvl"},
		func(p *ContainerProperty) string {
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
		nil,
		valHdlrMap{
			"root":  genSetValHdlr(C.DAOS_PROP_PERF_DOMAIN_ROOT),
			"group": genSetValHdlr(C.DAOS_PROP_PERF_DOMAIN_GROUP),
		},
		nil,
		func(p *ContainerProperty) string {
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
	// ----------------------------------------
	// Read-only properties go below this line.
	// ----------------------------------------
	C.DAOS_PROP_ENTRY_LAYOUT_TYPE: {
		C.DAOS_PROP_CO_LAYOUT_TYPE,
		"Layout Type",
		nil,
		nil,
		nil,
		func(p *ContainerProperty) string {
			var loStr [10]C.char
			loInt := C.ushort(p.GetValue())

			C.daos_unparse_ctype(loInt, &loStr[0])
			return fmt.Sprintf("%s (%d)", C.GoString(&loStr[0]), loInt)
		},
		true,
	},
	C.DAOS_PROP_ENTRY_LAYOUT_VER: {
		C.DAOS_PROP_CO_LAYOUT_VER,
		"Layout Version",
		nil,
		nil,
		nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_SNAPSHOT_MAX: {
		C.DAOS_PROP_CO_SNAPSHOT_MAX,
		"Max Snapshot",
		nil,
		nil,
		nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_ALLOCED_OID: {
		C.DAOS_PROP_CO_ALLOCED_OID,
		"Highest Allocated OID",
		nil,
		nil,
		nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_OWNER: {
		C.DAOS_PROP_CO_OWNER,
		"Owner",
		nil,
		nil,
		nil,
		strValStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_GROUP: {
		C.DAOS_PROP_CO_OWNER_GROUP,
		"Group",
		nil,
		nil,
		nil,
		strValStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_GLOBAL_VERSION: {
		C.DAOS_PROP_CO_GLOBAL_VERSION,
		"Global Version",
		nil,
		nil,
		nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_OBJ_VERSION: {
		C.DAOS_PROP_CO_OBJ_VERSION,
		"Object Version",
		nil,
		nil,
		nil,
		uintStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_ACL: {
		C.DAOS_PROP_CO_ACL,
		"Access Control List",
		nil,
		nil,
		nil,
		aclStringer,
		true,
	},
	C.DAOS_PROP_ENTRY_ROOT_OIDS: {
		C.DAOS_PROP_CO_ROOTS,
		"Root OIDs",
		nil,
		nil,
		nil,
		func(p *ContainerProperty) string {
			roots, err := getPropRootOids(p, true)
			if err != nil {
				return err.Error()
			}

			rootStrs := make([]string, len(roots))
			for i, oid := range roots {
				rootStrs[i] = oid.String()
			}
			return strings.Join(rootStrs, ", ")
		},
		true,
	},
	C.DAOS_PROP_ENTRY_SCRUB_DISABLED: {
		C.DAOS_PROP_CO_SCRUBBER_DISABLED,
		"Scrubber Disabled",
		nil,
		nil,
		nil,
		boolStringer,
		true,
	},
}

func (p *ContainerProperty) MarshalJSON() ([]byte, error) {
	if p == nil || p.entry == nil || p.hdlr == nil {
		return nil, errors.New("nil property")
	}

	// Normal case, just use the stringer.
	jsonValue := func(p *ContainerProperty) any {
		return p.hdlr.toString(p)
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
		jsonValue = func(p *ContainerProperty) any {
			return p.GetValue()
		}
	case ContainerPropACL:
		jsonValue = func(p *ContainerProperty) any {
			return getAclStrings(p)
		}
	case ContainerPropRootObjects:
		oids, err := getPropRootOids(p, true)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to get root OIDs for %s", p.Name)
		}
		jsonValue = func(_ *ContainerProperty) any {
			oidStrs := make([]string, len(oids))
			for i, oid := range oids {
				oidStrs[i] = oid.String()
			}
			return oidStrs
		}
	}

	type toJSON ContainerProperty
	return json.Marshal(&struct {
		Value any `json:"value"`
		*toJSON
	}{
		Value:  jsonValue(p),
		toJSON: (*toJSON)(p),
	})
}

func cksumHdlr(p *ContainerProperty, v string) error {
	csumVal, free := toCString(v)
	defer free()
	csumType := C.daos_str2csumcontprop(csumVal)

	if csumType < 0 {
		return propError("unknown checksum type %q", v)
	}

	return p.SetValue(uint64(csumType))
}

func compressHdlr(p *ContainerProperty, v string) error {
	compVal, free := toCString(v)
	defer free()
	compType := C.daos_str2compresscontprop(compVal)

	if compType < 0 {
		return propError("unknown compression type %q", v)
	}

	return p.SetValue(uint64(compType))
}

func encryptHdlr(p *ContainerProperty, v string) error {
	encVal, free := toCString(v)
	defer free()
	encType := C.daos_str2encryptcontprop(encVal)

	if encType < 0 {
		return propError("unknown encryption type %q", v)
	}

	return p.SetValue(uint64(encType))
}

func getPropRootOids(p *ContainerProperty, skipZero bool) (oids []ObjectID, _ error) {
	roots := (*C.struct_daos_prop_co_roots)(p.GetValuePtr())
	if roots == nil {
		return nil, errors.New("invalid OID roots pointer")
	}

	for _, cOid := range unsafe.Slice(&roots.cr_oids[0], 4) {
		oid := ObjectID(cOid)
		if skipZero && oid.IsZero() {
			continue
		}
		oids = append(oids, oid)
	}

	return
}

// --------------------------------------------------------------------------
// NB: Most new properties should not require modification to the code below.
// --------------------------------------------------------------------------

type (
	// ContainerProperty provides a Go wrapper around a DAOS container property list entry.
	ContainerProperty struct {
		property
		hdlr        *propHdlr
		Type        ContainerPropType `json:"-"`
		Name        string            `json:"name"`
		Description string            `json:"description"`
	}

	// ContainerPropertyList provides a Go wrapper around a DAOS container property list.
	ContainerPropertyList struct {
		propMap map[ContainerPropType]struct{}
		propertyList
	}
)

// AllocateContainerPropertyList performs an allocation of a daos_prop_t array
// for use with container property lists. The property list must be freed in order
// to avoid leaking memory.
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
		propMap:      make(map[ContainerPropType]struct{}),
		propertyList: *pl,
	}, nil
}

// NewContainerPropertyList allocates a property list and adds entries for the
// supplied property names. If no property names are specified, a list is created
// for all defined container properties. The property list must be freed in order
// to avoid leaking memory.
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

// CopyContainerPropertyList copies the source property list into the destination property list.
// NB: The destination list must have been allocated with enough free entries to accommodate the
// source list's properties.
func CopyContainerPropertyList(source, dest *ContainerPropertyList) error {
	if source == nil {
		return errors.New("nil source property list")
	}
	if dest == nil {
		return errors.New("nil destination property list")
	}
	if dest.immutable {
		return ErrPropertyListImmutable
	}

	availEntries := len(dest.entries) - int(dest.cProps.dpp_nr)
	if availEntries < int(source.cProps.dpp_nr) {
		return errors.Errorf("destination property list does not have enough room (%d < %d)", availEntries, source.cProps.dpp_nr)
	}

	for oi := 0; oi < int(source.cProps.dpp_nr); oi++ {
		if rc := C.daos_prop_entry_copy(&source.entries[oi], &dest.entries[dest.cProps.dpp_nr]); rc != 0 {
			return Status(rc)
		}
		dest.cProps.dpp_nr++
	}

	return nil
}

// CopyFrom is a helper method to copy the properties from another property list into this one.
func (cpl *ContainerPropertyList) CopyFrom(other *ContainerPropertyList) error {
	return CopyContainerPropertyList(other, cpl)
}

// DelEntryByType deletes an entry for the specified property type.
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

// MustAddEntryByType is a wrapper around AddEntryByType that panics on error.
func (cpl *ContainerPropertyList) MustAddEntryByType(propType ContainerPropType) *ContainerProperty {
	prop, err := cpl.AddEntryByType(propType)
	if err != nil {
		panic(err)
	}
	return prop
}

// AddEntryByType adds an entry for the specified property type.
func (cpl *ContainerPropertyList) AddEntryByType(propType ContainerPropType) (*ContainerProperty, error) {
	if cpl.immutable {
		return nil, ErrPropertyListImmutable
	}
	if _, found := cpl.propMap[propType]; found {
		return nil, errors.Errorf("duplicate property %q", propType)
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
	cpl.propMap[propType] = struct{}{}

	return prop, nil
}

// MustAddEntryByName is a wrapper around AddEntryByName that panics on error.
func (cpl *ContainerPropertyList) MustAddEntryByName(name string) *ContainerProperty {
	prop, err := cpl.AddEntryByName(name)
	if err != nil {
		panic(err)
	}
	return prop
}

// AddEntryByName adds an entry for the specified property name.
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

// PropertyNames returns a slice of property names, including any deprecated
// property names. If excReadOnly is true, read-only properties will be excluded
// from the returned slice.
func (cpl *ContainerPropertyList) PropertyNames(excReadOnly bool) (keys []string) {
	keys = make([]string, 0, len(cpl.entries))
	for _, prop := range cpl.Properties() {
		if prop.IsReadOnly() && excReadOnly {
			continue
		}

		keys = append(keys, prop.Name)
		if prop.hdlr.deprecatedNames != nil {
			keys = append(keys, prop.hdlr.deprecatedNames...)
		}
	}
	sort.Strings(keys)
	return
}

// Properties returns a slice of container property entries.
func (cpl *ContainerPropertyList) Properties() (props []*ContainerProperty) {
	for i := range cpl.entries {
		props = append(props, newContainerProperty(i, &cpl.entries[i]))
	}
	return
}

// newContainerProperty returns an initialized container property.
func newContainerProperty(listIdx int, entry *C.struct_daos_prop_entry) *ContainerProperty {
	propType := ContainerPropType(entry.dpe_type)
	hdlr := propHdlrs[propType.String()]

	return &ContainerProperty{
		property: property{
			idx:   C.int(listIdx),
			entry: entry,
		},
		hdlr:        hdlr,
		Type:        propType,
		Name:        propType.String(),
		Description: hdlr.shortDesc,
	}
}

// IsReadOnly returns true if the property is read-only.
func (cp *ContainerProperty) IsReadOnly() bool {
	return cp.hdlr.readOnly
}

// Set accepts a string value and attempts to set the underlying property via
// the property's handler. Returns an error if the property is read-only or
// the value is invalid for the property.
func (cp *ContainerProperty) Set(value string) error {
	if cp.IsReadOnly() {
		return errors.Errorf("property %q is read-only", cp.Name)
	}

	return cp.hdlr.execute(cp, value)
}

// StringValue returns a string representation of the property's value.
func (cp *ContainerProperty) StringValue() string {
	if cp.IsUnset() {
		return "not set"
	}
	if cp.hdlr.toString == nil {
		// Should be caught by unit test.
		panic(fmt.Sprintf("%s: no toString function set", cp.Name))
	}

	return cp.hdlr.toString(cp)
}

// String returns a string representation of the property suitable for debug
// logging.
func (cp *ContainerProperty) String() string {
	return fmt.Sprintf("%s: %s", cp.Name, cp.StringValue())
}

// SettableValues returns a slice of strings corresponding to the set of
// predefined valid values for the property, if any.
func (p *ContainerProperty) SettableValues() []string {
	if p.hdlr == nil || p.hdlr.valHdlrs == nil {
		return nil
	}

	return p.hdlr.valHdlrs.keys()
}

type propHdlr struct {
	// dpeType holds the property type (must be set).
	dpeType C.enum_daos_cont_props
	// shortDesc holds a short description of the property
	// to be used in human-readable output.
	shortDesc string
	// nameHdlr holds a closure for processing the entry.
	// If not set, and valHdlrs is set, then a default
	// handler for resolving the user input will be
	// used. If valHdlrs is not set, then the property
	// must be read-only. Optional.
	nameHdlr entryHdlr
	// valHdlrs defines a map of string-based property values
	// that should be resolved into one of the entry's
	// union values. Optional.
	valHdlrs valHdlrMap
	// deprecatedNames defines a list of deprecated names for
	// the property. Optional.
	deprecatedNames []string
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
type entryStringer func(*ContainerProperty) string

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
		if ph.valHdlrs == nil {
			// Should be caught by unit tests -- ensure that we don't ship with missing handlers.
			panic(fmt.Sprintf("%s: define a custom input handler or a map of valid inputs", p.Name))
		}

		vh, err := ph.valHdlrs.get(p.Name, v)
		if err != nil {
			return err
		}

		return vh(p, v)
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
			if ph.deprecatedNames != nil {
				if slices.Contains(ph.deprecatedNames, searchKey) {
					return ph, nil
				}
			}
		}
		return nil, propError("unknown property %q (valid: %s)", prop, strings.Join(phm.keys(), ","))
	}
	return ph, nil
}

func genSetValHdlr(v uint64) valHdlr {
	return func(p *ContainerProperty, _ string) error {
		return p.SetValue(v)
	}
}

func propInvalidValue(p *ContainerProperty) string {
	return fmt.Sprintf("property %q: invalid value 0x%x", p.Name, p.GetValue())
}

func propError(fs string, args ...any) error {
	return errors.Errorf("properties: "+fs, args...)
}

func debugStringer(p *ContainerProperty) string {
	if p.IsUnset() {
		return "not set"
	}

	return fmt.Sprintf("property %q: %+v", p.Name, p.entry)
}

func uintStringer(p *ContainerProperty) string {
	if p.IsUnset() {
		return "not set"
	}

	return fmt.Sprintf("%d", p.GetValue())
}

func boolStringer(p *ContainerProperty) string {
	if p.IsUnset() {
		return "not set"
	}

	switch p.GetValue() {
	case 0:
		return "false"
	case 1:
		return "true"
	default:
		return propInvalidValue(p)
	}
}

func humanSizeStringer(p *ContainerProperty) string {
	if p.IsUnset() {
		return "not set"
	}

	return humanize.IBytes(p.GetValue())
}

var hssFn = humanSizeStringer

func strValStringer(p *ContainerProperty) string {
	if p.IsUnset() {
		return "not set"
	}

	str := p.GetString()
	if str == "" {
		return "not set"
	}
	return str
}

func aclStringer(p *ContainerProperty) string {
	if p.IsUnset() {
		return "not set"
	}

	return strings.Join(getAclStrings(p), ", ")
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

	for _, ace := range unsafe.Slice(aces, int(acesNr)) {
		out = append(out, C.GoString(ace))
	}

	return
}
