//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build fault_injection
// +build fault_injection

package main

/*
#include <daos/common.h>
#include <daos_mgmt.h>
*/
import "C"
import (
	"fmt"
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

type faultsCmdRoot struct {
	Faults faultsCmd `command:"faults" description:"Inject server faults"`
}

type faultsCmd struct {
	Param     debugFaultCmd     `command:"set-param" description:"Use daos_debug_set_params() to set fault parameters"`
	Container containerFaultCmd `command:"container" description:"Inject container fault"`
}

type faultFrequency uint64

func (ff *faultFrequency) UnmarshalFlag(fv string) error {
	switch strings.ToLower(strings.TrimSpace(fv)) {
	case "always":
		*ff = faultFrequency(C.DAOS_FAIL_ALWAYS)
	case "once":
		*ff = faultFrequency(C.DAOS_FAIL_ONCE)
	default:
		v, err := strconv.ParseUint(fv, 10, 16)
		if err != nil {
			errors.Errorf("invalid fault frequency %q", fv)
		}
		*ff = faultFrequency(C.DAOS_FAIL_SOME | C.uint64_t(v))
	}
	return nil
}

func (ff faultFrequency) HasSome() (uint64, bool) {
	if ff&C.DAOS_FAIL_SOME != 0 {
		return uint64(ff &^ C.DAOS_FAIL_SOME), true
	}
	return 0, false
}

type faultLocation uint64

func (fl *faultLocation) UnmarshalFlag(fv string) error {
	// Ugh. Seems like there should be a more clever way to do this...
	switch strings.TrimSpace(fv) {
	case "DAOS_CHK_CONT_ORPHAN":
		*fl = faultLocation(C.DAOS_CHK_CONT_ORPHAN)
	case "DAOS_CHK_CONT_BAD_LABEL":
		*fl = faultLocation(C.DAOS_CHK_CONT_BAD_LABEL)
	default:
		return errors.Errorf("unhandled fault location %q", fv)
	}

	return nil
}

type faultRank uint32

func (fr *faultRank) UnmarshalFlag(fv string) error {
	if fv == strconv.FormatUint(uint64(C.CRT_NO_RANK), 10) || fv == "-1" {
		*fr = faultRank(C.CRT_NO_RANK)
		return nil
	}

	v, err := strconv.ParseUint(fv, 10, 32)
	if err != nil {
		return errors.Errorf("invalid rank %q", fv)
	}
	*fr = faultRank(v)
	return nil
}

type faultInjectionCmd struct {
	daosCmd

	Rank      faultRank      `short:"r" long:"rank" description:"Rank to inject fault on" default:"4294967295"`
	Frequency faultFrequency `short:"f" long:"frequency" description:"Fault injection frequency" choices:"always,once" default:"once"`
	Location  faultLocation  `short:"l" long:"location" description:"Fault injection location" required:"1"`
}

func (cmd *faultInjectionCmd) setParams() error {
	faultMask := C.uint64_t(cmd.Location)
	if someVal, hasSome := cmd.Frequency.HasSome(); hasSome {
		cmd.Debugf("setting fault injection frequency to %d", someVal)
		rc := C.daos_debug_set_params(nil, C.d_rank_t(cmd.Rank), C.DMG_KEY_FAIL_NUM, C.uint64_t(someVal), 0, nil)
		if err := daosError(rc); err != nil {
			return errors.Wrap(err, "failed to set fault injection frequency")
		}
		faultMask |= C.DAOS_FAIL_SOME
	} else {
		faultMask |= C.uint64_t(cmd.Frequency)
	}

	rankMsg := "all ranks"
	if cmd.Rank != C.CRT_NO_RANK {
		rankMsg = fmt.Sprintf("rank %d", cmd.Rank)
	}
	cmd.Debugf("injecting fault %d on %s", faultMask, rankMsg)
	rc := C.daos_debug_set_params(nil, C.d_rank_t(cmd.Rank), C.DMG_KEY_FAIL_LOC, faultMask, 0, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrap(err, "failed to set fault injection")
	}
	return nil
}

type debugFaultCmd struct {
	faultInjectionCmd
}

func (cmd *debugFaultCmd) Execute(_ []string) error {
	return cmd.setParams()
}

type containerFaultCmd struct {
	existingContainerCmd
	faultInjectionCmd
}

func (cmd *containerFaultCmd) Execute(_ []string) error {
	if err := cmd.setParams(); err != nil {
		return err
	}

	// Quick hack; find a more maintainable solution for this later.
	switch cmd.Location {
	case faultLocation(C.DAOS_CHK_CONT_ORPHAN):
		cdCmd := containerDestroyCmd{
			existingContainerCmd: cmd.existingContainerCmd,
		}
		cdCmd.Logger = cmd.Logger
		return cdCmd.Execute(nil)
	case faultLocation(C.DAOS_CHK_CONT_BAD_LABEL):
		cspCmd := containerSetPropCmd{
			existingContainerCmd: cmd.existingContainerCmd,
		}
		if err := cspCmd.Args.Props.UnmarshalFlag("label:new-label"); err != nil {
			return err
		}
		cspCmd.Logger = cmd.Logger
		return cspCmd.Execute(nil)
	}
	return nil
}
