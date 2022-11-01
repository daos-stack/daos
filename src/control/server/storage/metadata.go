//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"fmt"
	"path/filepath"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

type (
	// MetadataProvider is an interface for interacting with control plane metadata storage.
	MetadataProvider interface {
		Format(MetadataFormatRequest) error
		NeedsFormat(MetadataFormatRequest) (bool, error)
		Mount(MetadataMountRequest) (*MountResponse, error)
		Unmount(MetadataMountRequest) (*MountResponse, error)
	}

	// MetadataFormatReq is a request defining parameters for control plane metadata storage format.
	MetadataFormatRequest struct {
		pbin.ForwardableRequest
		RootPath   string
		Device     string
		DataPath   string
		OwnerUID   int
		OwnerGID   int
		EngineIdxs []uint
	}

	// MetadataMountReq is a request defining parameters for control plane metadata storage mount or unmount.
	MetadataMountRequest struct {
		pbin.ForwardableRequest
		RootPath string
		Device   string
	}
)

// MetadataForwarder forwards requests to the DAOS admin binary.
type MetadataForwarder struct {
	pbin.Forwarder
}

// NewMetadataForwarder creates a new MetadataForwarder.
func NewMetadataForwarder(log logging.Logger) *MetadataForwarder {
	pf := pbin.NewForwarder(log, pbin.DaosPrivHelperName)

	return &MetadataForwarder{
		Forwarder: *pf,
	}
}

// Mount forwards a metadata mount request.
func (f *MetadataForwarder) Mount(req MetadataMountRequest) (*MountResponse, error) {
	req.Forwarded = true

	res := new(MountResponse)
	if err := f.SendReq("MetadataMount", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Unmount forwards a metadata unmount request.
func (f *MetadataForwarder) Unmount(req MetadataMountRequest) (*MountResponse, error) {
	req.Forwarded = true

	res := new(MountResponse)
	if err := f.SendReq("MetadataUnmount", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Format forwards a request request to format a metadata device.
func (f *MetadataForwarder) Format(req MetadataFormatRequest) error {
	req.Forwarded = true

	var emptyResp struct{}
	if err := f.SendReq("MetadataFormat", req, &emptyResp); err != nil {
		return err
	}

	return nil
}

// NeedsFormat forwards a request request to check whether the metadata storage is formatted.
func (f *MetadataForwarder) NeedsFormat(req MetadataFormatRequest) (bool, error) {
	req.Forwarded = true

	var res bool
	if err := f.SendReq("MetadataNeedsFormat", req, &res); err != nil {
		return false, err
	}

	return res, nil
}

// ControlMetadataEngineDir generates a properly formatted path to engine specific control plane metadata.
func ControlMetadataEngineDir(mdPath string, engineIdx uint) string {
	if mdPath == "" {
		return ""
	}
	return filepath.Join(mdPath, fmt.Sprintf("engine%d", engineIdx))
}
