//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// This file imports all of the DAOS status codes.

package drpc

import "fmt"

/*
#cgo LDFLAGS: -lgurt

#include <daos_errno.h>
*/
import "C"

// DaosStatus is a status code in the set defined by the DAOS data plane.
type DaosStatus int32

func (ds DaosStatus) Error() string {
	// NB: Currently, d_errstr() just returns a string of the status
	// name, e.g. -1007 -> "DER_NOSPACE". This is better than nothing,
	// but hopefully d_errstr() will be extended to provide better strings
	// similar to perror().
	dErrStr := C.GoString(C.d_errstr(C.int(ds)))
	return fmt.Sprintf("DAOS error (%d): %s", ds, dErrStr)
}

const (
	// DaosSuccess indicates no error
	DaosSuccess DaosStatus = 0
	// DaosNoPermission indicates that access to a resource was denied
	DaosNoPermission DaosStatus = -C.DER_NO_PERM
	// DaosNoHandle indicates the handle was invalid
	DaosNoHandle DaosStatus = -C.DER_NO_HDL
	// DaosInvalidInput indicates an input was invalid
	DaosInvalidInput DaosStatus = -C.DER_INVAL
	// DaosExists indicates the entity already exists
	DaosExists DaosStatus = -C.DER_EXIST
	// DaosNonexistant indicates the entity does not exist
	DaosNonexistant DaosStatus = -C.DER_NONEXIST
	// DaosUnreachable indicates a node was unreachable
	DaosUnreachable DaosStatus = -C.DER_UNREACH
	// DaosNoSpace indicates there was not enough storage space
	DaosNoSpace DaosStatus = -C.DER_NOSPACE
	// DaosAlready indicates the operation was already done
	DaosAlready DaosStatus = -C.DER_ALREADY
	// DaosNoMemory indicates the system ran out of memory
	DaosNoMemory DaosStatus = -C.DER_NOMEM
	// DaosNotImpl indicates the requested functionality is not implemented
	DaosNotImpl DaosStatus = -C.DER_NOSYS
	// DaosTimedOut indicates the operation timed out
	DaosTimedOut DaosStatus = -C.DER_TIMEDOUT
	// DaosBusy indicates the system was busy and didn't process the request
	DaosBusy DaosStatus = -C.DER_BUSY
	// DaosTryAgain indicates the operation failed, but should be tried again
	DaosTryAgain DaosStatus = -C.DER_AGAIN
	// DaosProtocolError indicates incompatibility in communications protocols
	DaosProtocolError DaosStatus = -C.DER_PROTO
	// DaosNotInit indicates something in the system wasn't initialized
	DaosNotInit DaosStatus = -C.DER_UNINIT
	// DaosBufTooSmall indicates a provided buffer was too small
	DaosBufTooSmall DaosStatus = -C.DER_TRUNC
	// DaosStructTooSmall indicates data could not fit in the provided structure
	DaosStructTooSmall DaosStatus = -C.DER_OVERFLOW
	// DaosCanceled indicates the operation was canceled
	DaosCanceled DaosStatus = -C.DER_CANCELED
	// DaosOutOfGroup indicates that a rank wasn't found in the group
	DaosOutOfGroup DaosStatus = -C.DER_OOG
	// DaosMercuryError indicates that there was an error in the Mercury transport layer
	DaosMercuryError DaosStatus = -C.DER_HG
	// DaosUnregistered indicates that a requested RPC was not registered
	DaosUnregistered DaosStatus = -C.DER_UNREG
	// DaosAddrStringFailed indicates that an address string couldn't be generated
	DaosAddrStringFailed DaosStatus = -C.DER_ADDRSTR_GEN
	// DaosPMIXError indicates an error in the PMIX layer
	DaosPMIXError DaosStatus = -C.DER_PMIX
	// DaosIVCallback indicates that the IV callback cannot be handled locally
	DaosIVCallback DaosStatus = -C.DER_IVCB_FORWARD
	// DaosMiscError indicates an unspecified error
	DaosMiscError DaosStatus = -C.DER_MISC
	// DaosBadPath indicates that a bad file or directory path was provided
	DaosBadPath DaosStatus = -C.DER_BADPATH
	// DaosNotDir indicates that the path is not to a directory
	DaosNotDir DaosStatus = -C.DER_NOTDIR
	// DaosCorpcIncomplete indicates that corpc failed
	DaosCorpcIncomplete DaosStatus = -C.DER_CORPC_INCOMPLETE
	// DaosNoRASRank indicates that no rank is subscribed to RAS
	DaosNoRASRank DaosStatus = -C.DER_NO_RAS_RANK
	// DaosNotAttached indicates that a service group is not attached
	DaosNotAttached DaosStatus = -C.DER_NOTATTACH
	// DaosMismatch indicates a version mismatch
	DaosMismatch DaosStatus = -C.DER_MISMATCH
	// DaosEvicted indicates that the rank was evicted
	DaosEvicted DaosStatus = -C.DER_EVICTED
	// DaosNoReply indicates that there was no reply to an RPC
	DaosNoReply DaosStatus = -C.DER_NOREPLY
	// DaosDenialOfService indicates that there was a denial of service
	DaosDenialOfService DaosStatus = -C.DER_DOS
	// DaosBadTarget indicates that the target was wrong for the RPC
	DaosBadTarget DaosStatus = -C.DER_BAD_TARGET
	// DaosGroupVersionMismatch indicates that group versions didn't match
	DaosGroupVersionMismatch DaosStatus = -C.DER_GRPVER
)

const (
	// DaosIOError indicates a generic IO error
	DaosIOError DaosStatus = -C.DER_IO
	// DaosFreeMemError indicates an error freeing memory
	DaosFreeMemError DaosStatus = -C.DER_FREE_MEM
	// DaosNoEntry indicates that the entry was not found
	DaosNoEntry DaosStatus = -C.DER_ENOENT
	// DaosUnknownType indicates that the entity type was unknown
	DaosUnknownType DaosStatus = -C.DER_NOTYPE
	// DaosUnknownSchema indicates that the entity schema was unknown
	DaosUnknownSchema DaosStatus = -C.DER_NOSCHEMA
	// DaosNotLocal indicates that the entity was not local
	DaosNotLocal DaosStatus = -C.DER_NOLOCAL
	// DaosStale indicates that a resource was stale
	DaosStale DaosStatus = -C.DER_STALE
	// DaosNotLeader indicates that the replica is not the service leader
	DaosNotLeader DaosStatus = -C.DER_NOTLEADER
	// DaosTargetCreateError indicates that target creation failed
	DaosTargetCreateError DaosStatus = -C.DER_TGT_CREATE
	// DaosEpochReadOnly indicates that the epoch couldn't be modified
	DaosEpochReadOnly DaosStatus = -C.DER_EP_RO
	// DaosEpochRecycled indicates that the epoch was recycled due to age
	DaosEpochRecycled DaosStatus = -C.DER_EP_OLD
	// DaosKeyTooBig indicates that the key is too big
	DaosKeyTooBig DaosStatus = -C.DER_KEY2BIG
	// DaosRecordTooBig indicates that the record is too big
	DaosRecordTooBig DaosStatus = -C.DER_REC2BIG
	// DaosIOInvalid indicates a mismatch between IO buffers and object extents
	DaosIOInvalid DaosStatus = -C.DER_IO_INVAL
	// DaosEventQueueBusy indicates that the event queue is busy
	DaosEventQueueBusy DaosStatus = -C.DER_EQ_BUSY
	// DaosDomainMismatch indicates that there was a mismatch of domains in cluster components
	DaosDomainMismatch DaosStatus = -C.DER_DOMAIN
	// DaosShutdown indicates that the service should shut down
	DaosShutdown DaosStatus = -C.DER_SHUTDOWN
	// DaosInProgress indicates that the operation is in progress
	DaosInProgress DaosStatus = -C.DER_INPROGRESS
	// DaosNotApplicable indicates that the operation is not applicable
	DaosNotApplicable DaosStatus = -C.DER_NOTAPPLICABLE
	// DaosNotReplica indicates that the requested component is not a service replica
	DaosNotReplica DaosStatus = -C.DER_NOTREPLICA
	// DaosChecksumError indicates a checksum error
	DaosChecksumError DaosStatus = -C.DER_CSUM
)
