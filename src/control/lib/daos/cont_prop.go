//
// (C) Copyright 2019-2025 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"fmt"
	"strings"
	"unsafe"

	"github.com/pkg/errors"
)

/*
#cgo LDFLAGS: -ldaos_common -lgurt -lcart
#include <daos_prop.h>
#include <daos/cont_props.h>
*/
import "C"

const (
	// PropEntryAllocedOID is the highest allocated OID.
	PropEntryAllocedOID = C.DAOS_PROP_ENTRY_ALLOCED_OID
	// PropEntryChecksum is the checksum property.
	PropEntryChecksum = C.DAOS_PROP_ENTRY_CKSUM
	// PropEntryChecksumSize is the checksum size property.
	PropEntryChecksumSize = C.DAOS_PROP_ENTRY_CKSUM_SIZE
	// PropEntryCompression is the compression property.
	PropEntryCompression = C.DAOS_PROP_ENTRY_COMPRESS
	// PropEntryDedupe is the dedupe property.
	PropEntryDedupe = C.DAOS_PROP_ENTRY_DEDUP
	// PropEntryDedupThreshold is the dedupe threshold property.
	PropEntryDedupeThreshold = C.DAOS_PROP_ENTRY_DEDUP_THRESHOLD
	// PropEntryECCellSize is the EC cell size property.
	PropEntryECCellSize = C.DAOS_PROP_ENTRY_EC_CELL_SZ
	// PropEntryECPerfDomainAff is the EC performance domain affinity property.
	PropEntryECPerfDomainAff = C.DAOS_PROP_ENTRY_EC_PDA
	// PropEntryEncryption is the encryption property.
	PropEntryEncryption = C.DAOS_PROP_ENTRY_ENCRYPT
	// PropEntryGlobalVersion is the global version property.
	PropEntryGlobalVersion = C.DAOS_PROP_ENTRY_GLOBAL_VERSION
	// PropEntryObjectVersion is the object layout version property.
	PropEntryObjectVersion = C.DAOS_PROP_ENTRY_OBJ_VERSION
	// PropEntryGroup is the group property.
	PropEntryGroup = C.DAOS_PROP_ENTRY_GROUP
	// PropEntryLabel is the label property.
	PropEntryLabel = C.DAOS_PROP_ENTRY_LABEL
	// PropEntryLayout is the layout property.
	PropEntryLayoutType = C.DAOS_PROP_ENTRY_LAYOUT_TYPE
	// PropEntryLayoutVersion is the layout version property.
	PropEntryLayoutVersion = C.DAOS_PROP_ENTRY_LAYOUT_VER
	// PropEntryOwner is the owner property.
	PropEntryOwner = C.DAOS_PROP_ENTRY_OWNER
	// PropEntryRedunFactor is the redundancy factor property.
	PropEntryRedunFactor = C.DAOS_PROP_ENTRY_REDUN_FAC
	// PropEntryRedunLevel is the redundancy level property.
	PropEntryRedunLevel = C.DAOS_PROP_ENTRY_REDUN_LVL
	// PropEntryRedunPerfDomainAff is the redundancy performance domain affinity property.
	PropEntryRedunPerfDomainAff = C.DAOS_PROP_ENTRY_RP_PDA
	// PropEntrySnapshotMax is the snapshot max property.
	PropEntrySnapshotMax = C.DAOS_PROP_ENTRY_SNAPSHOT_MAX
	// PropEntryServerChecksum is the server checksum property.
	PropEntryServerChecksum = C.DAOS_PROP_ENTRY_SRV_CKSUM
	// PropEntryStatus is the status property.
	PropEntryStatus = C.DAOS_PROP_ENTRY_STATUS
)

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

func (cpt *ContainerPropType) FromString(in string) error {
	switch strings.TrimSpace(strings.ToLower(in)) {
	case C.DAOS_PROP_ENTRY_LABEL:
		*cpt = ContainerPropLabel
	case C.DAOS_PROP_ENTRY_LAYOUT_TYPE:
		*cpt = ContainerPropLayout
	case C.DAOS_PROP_ENTRY_LAYOUT_VER:
		*cpt = ContainerPropLayoutVersion
	case C.DAOS_PROP_ENTRY_CKSUM:
		*cpt = ContainerPropChecksumEnabled
	case C.DAOS_PROP_ENTRY_CKSUM_SIZE:
		*cpt = ContainerPropChecksumSize
	case C.DAOS_PROP_ENTRY_SRV_CKSUM:
		*cpt = ContainerPropChecksumSrvVrfy
	case C.DAOS_PROP_ENTRY_REDUN_FAC:
		*cpt = ContainerPropRedunFactor
	case C.DAOS_PROP_ENTRY_REDUN_LVL:
		*cpt = ContainerPropRedunLevel
	case C.DAOS_PROP_ENTRY_SNAPSHOT_MAX:
		*cpt = ContainerPropMaxSnapshots
	case C.DAOS_PROP_ENTRY_ACL:
		*cpt = ContainerPropACL
	case C.DAOS_PROP_ENTRY_COMPRESS:
		*cpt = ContainerPropCompression
	case C.DAOS_PROP_ENTRY_ENCRYPT:
		*cpt = ContainerPropEncrypted
	case C.DAOS_PROP_ENTRY_OWNER:
		*cpt = ContainerPropOwner
	case C.DAOS_PROP_ENTRY_GROUP:
		*cpt = ContainerPropGroup
	case C.DAOS_PROP_ENTRY_DEDUP:
		*cpt = ContainerPropDedupEnabled
	case C.DAOS_PROP_ENTRY_DEDUP_THRESHOLD:
		*cpt = ContainerPropDedupThreshold
	case C.DAOS_PROP_ENTRY_ROOT_OIDS:
		*cpt = ContainerPropRootObjects
	case C.DAOS_PROP_ENTRY_STATUS:
		*cpt = ContainerPropStatus
	case C.DAOS_PROP_ENTRY_ALLOCED_OID:
		*cpt = ContainerPropHighestOid
	case C.DAOS_PROP_ENTRY_EC_CELL_SZ:
		*cpt = ContainerPropEcCellSize
	case C.DAOS_PROP_ENTRY_EC_PDA:
		*cpt = ContainerPropEcPerfDom
	case C.DAOS_PROP_ENTRY_RP_PDA:
		*cpt = ContainerPropEcPerfDomAff
	case C.DAOS_PROP_ENTRY_GLOBAL_VERSION:
		*cpt = ContainerPropGlobalVersion
	case C.DAOS_PROP_ENTRY_SCRUB_DISABLED:
		*cpt = ContainerPropScubberDisabled
	case C.DAOS_PROP_ENTRY_OBJ_VERSION:
		*cpt = ContainerPropObjectVersion
	case C.DAOS_PROP_ENTRY_PERF_DOMAIN:
		*cpt = ContainerPropPerfDomain
	default:
		return fmt.Errorf("unknown container property type %q", in)
	}

	return nil
}

func NewContainerPropertyList(count uint) (*ContainerPropertyList, error) {
	pl, err := newPropertyList(count)
	if err != nil {
		return nil, err
	}

	return &ContainerPropertyList{
		propertyList: *pl,
	}, nil
}

func ContainerPropertyListFromPtr(ptr unsafe.Pointer) (*ContainerPropertyList, error) {
	pl, err := propertyListFromPtr(ptr)
	if err != nil {
		return nil, err
	}

	return &ContainerPropertyList{
		propertyList: *pl,
	}, nil
}

func (cpl *ContainerPropertyList) MustAddEntryType(propType ContainerPropType) {
	if err := cpl.AddEntryType(propType); err != nil {
		panic(err)
	}
}

func (cpl *ContainerPropertyList) AddEntryType(propType ContainerPropType) error {
	if propType < containerPropMin || propType > containerPropMax {
		return errors.Wrapf(InvalidInput, "invalid container property type %d", propType)
	}

	if int(cpl.cProps.dpp_nr) == len(cpl.entries) {
		return errors.Errorf("property list is full (%d/%d entries)", len(cpl.entries), cap(cpl.entries))
	}

	cpl.entries[cpl.cProps.dpp_nr].dpe_type = C.uint(propType)
	cpl.cProps.dpp_nr++

	return nil
}

func (cpl *ContainerPropertyList) Properties() (props []*ContainerProperty) {
	for i := range cpl.entries {
		props = append(props, &ContainerProperty{
			property: property{
				idx:   C.int(i),
				entry: &cpl.entries[i],
			},
		})
	}
	return
}

func (cp *ContainerProperty) Type() ContainerPropType {
	return ContainerPropType(cp.entry.dpe_type)
}

func (cp *ContainerProperty) Name() string {
	return cp.Type().String()
}

func (cp *ContainerProperty) String() string {
	return fmt.Sprintf("%s: %s", cp.Name(), cp.property.String())
}
