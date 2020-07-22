//
// (C) Copyright 2020 Intel Corporation.
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
)

const (
	// AnnotatedFaultType defines a stable identifier for Faults serialized as gRPC
	// status metadata.
	AnnotatedFaultType = "proto.fault.Fault"
	// AnnotatedDaosStatusType defines an identifier for DaosStatus errors serialized
	// as gRPC status metadata.
	AnnotatedDaosStatusType = "proto.drpc.DaosStatus"
)

// FaultFromMeta converts a map of metadata into a *fault.Fault.
func FaultFromMeta(meta map[string]string) (*fault.Fault, error) {
	jm, err := json.Marshal(meta)
	if err != nil {
		return nil, err
	}

	f := &fault.Fault{}
	err = json.Unmarshal(jm, f)
	return f, err
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

// AnnotateError adds more details to the gRPC error,
// if available.
func AnnotateError(in error) error {
	if f, isFault := errors.Cause(in).(*fault.Fault); isFault {
		out, attachErr := status.New(codes.Internal, f.Error()).
			WithDetails(&errdetails.ErrorInfo{
				Reason:   AnnotatedFaultType,
				Domain:   f.Domain,
				Metadata: MetaFromFault(f),
			})
		if attachErr == nil {
			return out.Err()
		}
	}
	if s, isStatus := errors.Cause(in).(drpc.DaosStatus); isStatus {
		out, attachErr := status.New(codes.Internal, s.Error()).
			WithDetails(&errdetails.ErrorInfo{
				Reason: AnnotatedDaosStatusType,
				Domain: "DAOS",
				Metadata: map[string]string{
					"Status": strconv.Itoa(int(s)),
				},
			})
		if attachErr == nil {
			return out.Err()
		}
	}

	return in
}

// UnwrapFault ranges through the status details, looking
// for the first Fault it can successfully return. Returns
// the original status as an error if no Fault is unwrapped.
func UnwrapFault(st *status.Status) (*fault.Fault, error) {
	if st == nil {
		return nil, nil
	}

	for _, detail := range st.Details() {
		switch t := detail.(type) {
		case *errdetails.ErrorInfo:
			if t.Reason == AnnotatedFaultType {
				return FaultFromMeta(t.Metadata)
			}
		}
	}
	return nil, st.Err()
}

// UnwrapDaosStatus ranges through the status details, looking
// for the first DaosStatus it can successfully return. Returns
// the original status as an error if no DaosStatus is unwrapped.
func UnwrapDaosStatus(st *status.Status) (drpc.DaosStatus, error) {
	if st == nil {
		return drpc.DaosSuccess, nil
	}

	for _, detail := range st.Details() {
		switch t := detail.(type) {
		case *errdetails.ErrorInfo:
			if t.Reason == AnnotatedDaosStatusType {
				i, err := strconv.Atoi(t.Metadata["Status"])
				if err != nil {
					return drpc.DaosMiscError, err
				}
				return drpc.DaosStatus(i), nil
			}
		}
	}
	return drpc.DaosMiscError, st.Err()
}
