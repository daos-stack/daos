//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// This file imports all of the DAOS status codes.

package daos

import "fmt"

/*
#cgo LDFLAGS: -lgurt

#include <daos_errno.h>
*/
import "C"

// Status is a status code in the set defined by the DAOS data plane.
type Status int32

func (ds Status) Error() string {
	dErrStr := C.GoString(C.d_errstr(C.int(ds)))
	dErrDesc := C.GoString(C.d_errdesc(C.int(ds)))
	return fmt.Sprintf("%s(%d): %s", dErrStr, ds, dErrDesc)
}

func (ds Status) Int32() int32 {
	return int32(ds)
}

const (
	// Success indicates no error
	Success Status = 0
	// NoPermission indicates that access to a resource was denied
	NoPermission Status = -C.DER_NO_PERM
	// NoHandle indicates the handle was invalid
	NoHandle Status = -C.DER_NO_HDL
	// InvalidInput indicates an input was invalid
	InvalidInput Status = -C.DER_INVAL
	// Exists indicates the entity already exists
	Exists Status = -C.DER_EXIST
	// Nonexistent indicates the entity does not exist
	Nonexistent Status = -C.DER_NONEXIST
	// Unreachable indicates a node was unreachable
	Unreachable Status = -C.DER_UNREACH
	// NoSpace indicates there was not enough storage space
	NoSpace Status = -C.DER_NOSPACE
	// Already indicates the operation was already done
	Already Status = -C.DER_ALREADY
	// NoMemory indicates the system ran out of memory
	NoMemory Status = -C.DER_NOMEM
	// NotImpl indicates the requested functionality is not implemented
	NotImpl Status = -C.DER_NOSYS
	// TimedOut indicates the operation timed out
	TimedOut Status = -C.DER_TIMEDOUT
	// Busy indicates the system was busy and didn't process the request
	Busy Status = -C.DER_BUSY
	// TryAgain indicates the operation failed, but should be tried again
	TryAgain Status = -C.DER_AGAIN
	// ProtocolError indicates incompatibility in communications protocols
	ProtocolError Status = -C.DER_PROTO
	// NotInit indicates something in the system wasn't initialized
	NotInit Status = -C.DER_UNINIT
	// BufTooSmall indicates a provided buffer was too small
	BufTooSmall Status = -C.DER_TRUNC
	// StructTooSmall indicates data could not fit in the provided structure
	StructTooSmall Status = -C.DER_OVERFLOW
	// Canceled indicates the operation was canceled
	Canceled Status = -C.DER_CANCELED
	// OutOfGroup indicates that a rank wasn't found in the group
	OutOfGroup Status = -C.DER_OOG
	// MercuryError indicates that there was an error in the Mercury transport layer
	MercuryError Status = -C.DER_HG
	// Unregistered indicates that a requested RPC was not registered
	Unregistered Status = -C.DER_UNREG
	// AddrStringFailed indicates that an address string couldn't be generated
	AddrStringFailed Status = -C.DER_ADDRSTR_GEN
	// PMIXError indicates an error in the PMIX layer
	PMIXError Status = -C.DER_PMIX
	// IVCallback indicates that the IV callback cannot be handled locally
	IVCallback Status = -C.DER_IVCB_FORWARD
	// MiscError indicates an unspecified error
	MiscError Status = -C.DER_MISC
	// BadPath indicates that a bad file or directory path was provided
	BadPath Status = -C.DER_BADPATH
	// NotDir indicates that the path is not to a directory
	NotDir Status = -C.DER_NOTDIR
	// CorpcIncomplete indicates that corpc failed
	CorpcIncomplete Status = -C.DER_CORPC_INCOMPLETE
	// NoRASRank indicates that no rank is subscribed to RAS
	NoRASRank Status = -C.DER_NO_RAS_RANK
	// NotAttached indicates that a service group is not attached
	NotAttached Status = -C.DER_NOTATTACH
	// Mismatch indicates a version mismatch
	Mismatch Status = -C.DER_MISMATCH
	// Excluded indicates that the rank was excluded
	Excluded Status = -C.DER_EXCLUDED
	// NoReply indicates that there was no reply to an RPC
	NoReply Status = -C.DER_NOREPLY
	// DenialOfService indicates that there was a denial of service
	DenialOfService Status = -C.DER_DOS
	// BadTarget indicates that the target was wrong for the RPC
	BadTarget Status = -C.DER_BAD_TARGET
	// GroupVersionMismatch indicates that group versions didn't match
	GroupVersionMismatch Status = -C.DER_GRPVER
	// NoService indicates the pool service is not up and didn't process the pool request
	NoService Status = -C.DER_NO_SERVICE
)

const (
	// IOError indicates a generic IO error
	IOError Status = -C.DER_IO
	// FreeMemError indicates an error freeing memory
	FreeMemError Status = -C.DER_FREE_MEM
	// NoEntry indicates that the entry was not found
	NoEntry Status = -C.DER_ENOENT
	// UnknownType indicates that the entity type was unknown
	UnknownType Status = -C.DER_NOTYPE
	// UnknownSchema indicates that the entity schema was unknown
	UnknownSchema Status = -C.DER_NOSCHEMA
	// NotLocal indicates that the entity was not local
	NotLocal Status = -C.DER_NOLOCAL
	// Stale indicates that a resource was stale
	Stale Status = -C.DER_STALE
	// NotLeader indicates that the replica is not the service leader
	NotLeader Status = -C.DER_NOTLEADER
	// TargetCreateError indicates that target creation failed
	TargetCreateError Status = -C.DER_TGT_CREATE
	// EpochReadOnly indicates that the epoch couldn't be modified
	EpochReadOnly Status = -C.DER_EP_RO
	// EpochRecycled indicates that the epoch was recycled due to age
	EpochRecycled Status = -C.DER_EP_OLD
	// KeyTooBig indicates that the key is too big
	KeyTooBig Status = -C.DER_KEY2BIG
	// RecordTooBig indicates that the record is too big
	RecordTooBig Status = -C.DER_REC2BIG
	// IOInvalid indicates a mismatch between IO buffers and object extents
	IOInvalid Status = -C.DER_IO_INVAL
	// EventQueueBusy indicates that the event queue is busy
	EventQueueBusy Status = -C.DER_EQ_BUSY
	// DomainMismatch indicates that there was a mismatch of domains in cluster components
	DomainMismatch Status = -C.DER_DOMAIN
	// Shutdown indicates that the service should shut down
	Shutdown Status = -C.DER_SHUTDOWN
	// InProgress indicates that the operation is in progress
	InProgress Status = -C.DER_INPROGRESS
	// NotApplicable indicates that the operation is not applicable
	NotApplicable Status = -C.DER_NOTAPPLICABLE
	// NotReplica indicates that the requested component is not a service replica
	NotReplica Status = -C.DER_NOTREPLICA
	// ChecksumError indicates a checksum error
	ChecksumError Status = -C.DER_CSUM
	// ControlIncompatible indicates that one or more control plane components are incompatible
	ControlIncompatible Status = -C.DER_CONTROL_INCOMPAT
	// NoCert indicates that one or more configured certificates could not be accessed.
	NoCert Status = -C.DER_NO_CERT
	// BadCert indicates that an invalid certificate was detected.
	BadCert Status = -C.DER_BAD_CERT
)
