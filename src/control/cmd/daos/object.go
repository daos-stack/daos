//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"unsafe"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
)

/*
#include "util.h"

#include <daos/object.h>
*/
import "C"

type objectCmd struct {
	Query objQueryCmd `command:"query" description:"query an object's layout"`
	//ListKeys objListKeysCmd `command:"list-keys" description:"list an object's keys"`
	//Dump     objDumpCmd     `command:"dump" description:"dump an object's contents"`
}

type objBaseCmd struct {
	existingContainerCmd

	ObjectIDFlag OidFlag `long:"oid" short:"i" description:"DAOS object id (deprecated; use positional arg)"`
	Args         struct {
		ObjectID OidFlag `positional-arg-name:"<DAOS object id (HI.LO)>"`
	} `positional-args:"yes"`
}

func (cmd *objBaseCmd) getOid() (C.daos_obj_id_t, error) {
	if cmd.ObjectIDFlag.Set && cmd.Args.ObjectID.Set {
		return C.daos_obj_id_t{}, errors.New("can't specify --oid and positional argument")
	}
	if cmd.ObjectIDFlag.Set {
		cmd.Args.ObjectID.Set = true
		cmd.Args.ObjectID.Oid = cmd.ObjectIDFlag.Oid
	}
	if !cmd.Args.ObjectID.Set {
		return C.daos_obj_id_t{}, &flags.Error{flags.ErrRequired, "OID is a required argument"}
	}

	return cmd.Args.ObjectID.Oid, nil
}

type objQueryCmd struct {
	objBaseCmd
}

type shardLocation struct {
	Rank   uint32 `json:"rank"`
	Target uint32 `json:"target"`
}

type objectShard struct {
	Replicas []*shardLocation `json:"replicas"`
}

type objectLayout struct {
	OID     C.daos_obj_id_t `json:"-"`
	Version uint32          `json:"version"`
	Class   string          `json:"class"`
	Shards  []*objectShard  `json:"shards"`
}

func (ol *objectLayout) MarshalJSON() ([]byte, error) {
	type toJSON objectLayout
	return json.Marshal(&struct {
		OID string `json:"oid"`
		*toJSON
	}{
		OID:    oidString(ol.OID),
		toJSON: (*toJSON)(ol),
	})
}

func shardLocations(shard *C.struct_daos_obj_shard) []*shardLocation {
	locations := make([]*shardLocation, shard.os_replica_nr)

	// https://github.com/golang/go/issues/11925#issuecomment-128405123
	// NB: https://pkg.go.dev/unsafe#Pointer dictates that this must be done in one step!
	locsSlice := unsafe.Slice((*C.struct_daos_shard_loc)(unsafe.Pointer(uintptr(unsafe.Pointer(shard))+C.sizeof_struct_daos_obj_shard)), shard.os_replica_nr)
	for i, cLoc := range locsSlice {
		locations[i] = &shardLocation{
			Rank:   uint32(cLoc.sd_rank),
			Target: uint32(cLoc.sd_tgt_idx),
		}
	}
	return locations
}

func layoutShards(layout *C.struct_daos_obj_layout) []*objectShard {
	shards := make([]*objectShard, layout.ol_nr)

	// https://github.com/golang/go/issues/11925#issuecomment-128405123
	// NB: https://pkg.go.dev/unsafe#Pointer dictates that this must be done in one step!
	shardSlice := unsafe.Slice((**C.struct_daos_obj_shard)(unsafe.Pointer(uintptr(unsafe.Pointer(layout))+C.sizeof_struct_daos_obj_layout)), int(layout.ol_nr))
	for i, cShard := range shardSlice {
		shards[i] = &objectShard{
			Replicas: shardLocations(cShard),
		}
	}
	return shards
}

func newObjLayout(oid C.daos_obj_id_t, layout *C.struct_daos_obj_layout) *objectLayout {
	var oclass [10]C.char
	C.daos_oclass_id2name(C.daos_obj_id2class(oid), &oclass[0])

	return &objectLayout{
		OID:     oid,
		Version: uint32(layout.ol_ver),
		Class:   C.GoString(&oclass[0]),
		Shards:  layoutShards(layout),
	}
}

func (cmd *objQueryCmd) Execute(_ []string) error {
	oid, err := cmd.getOid()
	if err != nil {
		return err
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	var cLayout *C.struct_daos_obj_layout
	if err := daosError(C.daos_obj_layout_get(ap.cont, oid, &cLayout)); err != nil {
		return errors.Wrapf(err, "failed to retrieve layout for object %s", cmd.Args.ObjectID.String())

	}
	defer C.daos_obj_layout_free(cLayout)

	layout := newObjLayout(oid, cLayout)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(layout, nil)
	}

	// TODO: Revisit this output to make it more non-developer friendly.
	// For the moment, retain compatibility with the old daos tool.
	cmd.Infof("oid: %s ver %d grp_nr: %d", oidString(oid), layout.Version, cLayout.ol_nr)
	for i, shard := range layout.Shards {
		cmd.Infof("grp: %d", i)
		for j, replica := range shard.Replicas {
			cmd.Infof("replica %d %d", j, replica.Rank)
		}
	}

	return nil
}
