//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

// This file imports all of the DAOS status codes.

package drpc

// #include <gurt/errno.h>
import "C"

// DaosStatus is a status code in the set defined by the DAOS data plane.
type DaosStatus int32

const (
	// DaosSuccess indicates no error
	DaosSuccess DaosStatus = 0
	// DaosNoPermission indicates that access to a resource was denied
	DaosNoPermission = -C.DER_NO_PERM
	// DaosNoHandle indicates the handle was invalid
	DaosNoHandle = -C.DER_NO_HDL
	// DaosInvalidInput indicates an input was invalid
	DaosInvalidInput = -C.DER_INVAL
	// DaosExists indicates the entity already exists
	DaosExists = -C.DER_EXIST
	// DaosNonexistant indicates the entity does not exist
	DaosNonexistant = -C.DER_NONEXIST
	// DaosUnreachable indicates a node was unreachable
	DaosUnreachable = -C.DER_UNREACH
	// DaosNoSpace indicates there was not enough storage space
	DaosNoSpace = -C.DER_NOSPACE
	// DaosAlready indicates the operation was already done
	DaosAlready = -C.DER_ALREADY
	// DaosNoMemory indicates the system ran out of memory
	DaosNoMemory = -C.DER_NOMEM
	// DaosNotImpl indicates the requested functionality is not implemented
	DaosNotImpl = -C.DER_NOSYS
	// DaosTimedOut indicates the operation timed out
	DaosTimedOut = -C.DER_TIMEDOUT
	// DaosBusy indicates the system was busy and didn't process the request
	DaosBusy = -C.DER_BUSY
	// DaosTryAgain indicates the operation failed, but should be tried again
	DaosTryAgain = -C.DER_AGAIN
	// DaosProtocolError indicates incompatibility in communications protocols
	DaosProtocolError = -C.DER_PROTO
	// DaosNotInit indicates something in the system wasn't initialized
	DaosNotInit = -C.DER_UNINIT
	// DaosBufTooSmall indicates a provided buffer was too small
	DaosBufTooSmall = -C.DER_TRUNC
	// DaosStructTooSmall indicates data could not fit in the provided structure
	DaosStructTooSmall = -C.DER_OVERFLOW
	// DaosCanceled indicates the operation was canceled
	DaosCanceled = -C.DER_CANCELED
	// DaosOutOfGroup indicates that a rank wasn't found in the group
	DaosOutOfGroup = -C.DER_OOG
	// DaosMercuryError indicates that there was an error in the Mercury transport layer
	DaosMercuryError = -C.DER_HG
	// DaosUnregistered indicates that a requested RPC was not registered
	DaosUnregistered = -C.DER_UNREG
	// DaosAddrStringFailed indicates that an address string couldn't be generated
	DaosAddrStringFailed = -C.DER_ADDRSTR_GEN
	// DaosPMIXError indicates an error in the PMIX layer
	DaosPMIXError = -C.DER_PMIX
	// DaosIVCallback indicates that the IV callback cannot be handled locally
	DaosIVCallback = -C.DER_IVCB_FORWARD
	// DaosMiscError indicates an unspecified error
	DaosMiscError = -C.DER_MISC
	// DaosBadPath indicates that a bad file or directory path was provided
	DaosBadPath = -C.DER_BADPATH
	// DaosNotDir indicates that the path is not to a directory
	DaosNotDir = -C.DER_NOTDIR
	// DaosCorpcIncomplete indicates that corpc failed
	DaosCorpcIncomplete = -C.DER_CORPC_INCOMPLETE
	// DaosNoRASRank indicates that no rank is subscribed to RAS
	DaosNoRASRank = -C.DER_NO_RAS_RANK
	// DaosNotAttached indicates that a service group is not attached
	DaosNotAttached = -C.DER_NOTATTACH
	// DaosMismatch indicates a version mismatch
	DaosMismatch = -C.DER_MISMATCH
	// DaosEvicted indicates that the rank was evicted
	DaosEvicted = -C.DER_EVICTED
	// DaosNoReply indicates that there was no reply to an RPC
	DaosNoReply = -C.DER_NOREPLY
	// DaosDenialOfService indicates that there was a denial of service
	DaosDenialOfService = -C.DER_DOS
	// DaosBadTarget indicates that the target was wrong for the RPC
	DaosBadTarget = -C.DER_BAD_TARGET
	// DaosGroupVersionMismatch indicates that group versions didn't match
	DaosGroupVersionMismatch = -C.DER_GRPVER
)

const (
	// DaosIOError indicates a generic IO error
	DaosIOError DaosStatus = -C.DER_IO
	// DaosFreeMemError indicates an error freeing memory
	DaosFreeMemError = -C.DER_FREE_MEM
	// DaosNoEntry indicates that the entry was not found
	DaosNoEntry = -C.DER_ENOENT
	// DaosUnknownType indicates that the entity type was unknown
	DaosUnknownType = -C.DER_NOTYPE
	// DaosUnknownSchema indicates that the entity schema was unknown
	DaosUnknownSchema = -C.DER_NOSCHEMA
	// DaosNotLocal indicates that the entity was not local
	DaosNotLocal = -C.DER_NOLOCAL
	// DaosStale indicates that a resource was stale
	DaosStale = -C.DER_STALE
	// DaosNotLeader indicates that the replica is not the service leader
	DaosNotLeader = -C.DER_NOTLEADER
	// DaosTargetCreateError indicates that target creation failed
	DaosTargetCreateError = -C.DER_TGT_CREATE
	// DaosEpochReadOnly indicates that the epoch couldn't be modified
	DaosEpochReadOnly = -C.DER_EP_RO
	// DaosEpochRecycled indicates that the epoch was recycled due to age
	DaosEpochRecycled = -C.DER_EP_OLD
	// DaosKeyTooBig indicates that the key is too big
	DaosKeyTooBig = -C.DER_KEY2BIG
	// DaosRecordTooBig indicates that the record is too big
	DaosRecordTooBig = -C.DER_REC2BIG
	// DaosIOInvalid indicates a mismatch between IO buffers and object extents
	DaosIOInvalid = -C.DER_IO_INVAL
	// DaosEventQueueBusy indicates that the event queue is busy
	DaosEventQueueBusy = -C.DER_EQ_BUSY
	// DaosDomainMismatch indicates that there was a mismatch of domains in cluster components
	DaosDomainMismatch = -C.DER_DOMAIN
	// DaosShutdown indicates that the service should shut down
	DaosShutdown = -C.DER_SHUTDOWN
	// DaosInProgress indicates that the operation is in progress
	DaosInProgress = -C.DER_INPROGRESS
	// DaosNotApplicable indicates that the operation is not applicable
	DaosNotApplicable = -C.DER_NOTAPPLICABLE
	// DaosNotReplica indicates that the requested component is not a service replica
	DaosNotReplica = -C.DER_NOTREPLICA
	// DaosChecksumError indicates a checksum error
	DaosChecksumError = -C.DER_CSUM
)
