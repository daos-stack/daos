//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package proto

import (
	"encoding/json"
	"strconv"

	"github.com/pkg/errors"
	"google.golang.org/genproto/googleapis/rpc/errdetails"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// AnnotatedFaultType defines a stable identifier for Faults serialized as gRPC
	// status metadata.
	AnnotatedFaultType = "proto.fault.Fault"
	// AnnotatedDaosStatusType defines an identifier for DaosStatus errors serialized
	// as gRPC status metadata.
	AnnotatedDaosStatusType = "proto.drpc.DaosStatus"
	// AnnotatedSystemErrNotLeader defines an identifier for ErrNotLeader errors
	// serialized as gRPC status metadata.
	AnnotatedSystemErrNotLeader = "proto.system.ErrNotLeader"
	// AnnotatedSystemErrNotReplica defines an identifier for ErrNotReplica errors
	// serialized as gRPC status metadata.
	AnnotatedSystemErrNotReplica = "proto.system.ErrNotReplica"
)

// ErrFromMeta converts a map of metadata into an error.
func ErrFromMeta(meta map[string]string, errType error) error {
	jm, err := json.Marshal(meta)
	if err != nil {
		return err
	}

	switch et := errType.(type) {
	case *fault.Fault:
		err = json.Unmarshal(jm, et)
	case *system.ErrNotReplica:
		err = json.Unmarshal([]byte(meta["Replicas"]), &et.Replicas)
	case *system.ErrNotLeader:
		et.LeaderHint = meta["LeaderHint"]
		err = json.Unmarshal([]byte(meta["Replicas"]), &et.Replicas)
	default:
		err = errors.Errorf("unable to convert %+v into error", meta)
	}

	if err != nil {
		return err
	}

	return errType
}

// MetaFromFault converts a *fault.Fault into a map of metadata.
func MetaFromFault(f *fault.Fault) map[string]string {
	return map[string]string{
		"Domain":      f.Domain,
		"Code":        strconv.Itoa(int(f.Code)),
		"Description": f.Description,
		"Reason":      f.Reason,
		"Resolution":  f.Resolution,
	}
}

// AnnotateError adds more details to the gRPC error, if available.
func AnnotateError(in error) error {
	var details *errdetails.ErrorInfo

	cause := errors.Cause(in)
	switch et := cause.(type) {
	case *fault.Fault:
		details = &errdetails.ErrorInfo{
			Reason:   AnnotatedFaultType,
			Domain:   et.Domain,
			Metadata: MetaFromFault(et),
		}
	case drpc.DaosStatus:
		details = &errdetails.ErrorInfo{
			Reason: AnnotatedDaosStatusType,
			Domain: "DAOS",
			Metadata: map[string]string{
				"Status": strconv.Itoa(int(et)),
			},
		}
	case *system.ErrNotReplica:
		data, err := json.Marshal(et.Replicas)
		if err != nil {
			break
		}
		details = &errdetails.ErrorInfo{
			Reason: AnnotatedSystemErrNotReplica,
			Domain: "DAOS",
			Metadata: map[string]string{
				"Replicas": string(data),
			},
		}
	case *system.ErrNotLeader:
		data, err := json.Marshal(et.Replicas)
		if err != nil {
			break
		}
		details = &errdetails.ErrorInfo{
			Reason: AnnotatedSystemErrNotLeader,
			Domain: "DAOS",
			Metadata: map[string]string{
				"LeaderHint": et.LeaderHint,
				"Replicas":   string(data),
			},
		}
	}

	if details == nil {
		return in
	}

	out, attachErr := status.New(codes.Internal, cause.Error()).WithDetails(details)
	if attachErr != nil {
		return in
	}

	return out.Err()
}

// UnwrapError reconstitutes the original error from the gRPC metadata.
func UnwrapError(st *status.Status) error {
	if st == nil {
		return nil
	}

	for _, detail := range st.Details() {
		switch t := detail.(type) {
		case *errdetails.ErrorInfo:
			switch t.Reason {
			case AnnotatedFaultType:
				return ErrFromMeta(t.Metadata, new(fault.Fault))
			case AnnotatedDaosStatusType:
				i, err := strconv.Atoi(t.Metadata["Status"])
				if err != nil {
					return err
				}
				return drpc.DaosStatus(i)
			case AnnotatedSystemErrNotReplica:
				return ErrFromMeta(t.Metadata, new(system.ErrNotReplica))
			case AnnotatedSystemErrNotLeader:
				return ErrFromMeta(t.Metadata, new(system.ErrNotLeader))
			}
		}
	}

	return st.Err()
}
