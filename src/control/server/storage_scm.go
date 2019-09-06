//
// (C) Copyright 2018-2019 Intel Corporation.
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

package server

import (
	"encoding/json"
	"fmt"
	"os/exec"
	"strconv"
	"strings"

	"github.com/pkg/errors"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	log "github.com/daos-stack/daos/src/control/logging"
)

//go:generate stringer -type=scmState
type scmState int

const (
	scmStateUnknown scmState = iota
	scmStateNoRegions
	scmStateFreeCapacity
	scmStateNoCapacity

	cmdScmShowRegions = "ipmctl show -d PersistentMemoryType,FreeCapacity -region"
	outScmNoRegions   = "\nThere are no Regions defined in the system.\n"
	// creates a AppDirect/Interleaved memory allocation goal across all DCPMMs on a system.
	cmdScmCreateRegions    = "ipmctl create -f -goal PersistentMemoryType=AppDirect"
	cmdScmRemoveRegions    = "ipmctl create -f -goal MemoryMode=100"
	cmdScmCreateNamespace  = "ndctl create-namespace" // returns json ns info
	cmdScmListNamespaces   = "ndctl list -N"          // returns json ns info
	cmdScmDisableNamespace = "ndctl disable-namespace %s"
	cmdScmDestroyNamespace = "ndctl destroy-namespace %s"

	MsgScmRebootRequired   = "A reboot is required to process new memory allocation goals."
	msgScmNoModules        = "no scm modules to prepare"
	msgScmNotInited        = "scm storage could not be accessed"
	msgScmAlreadyFormatted = "scm storage has already been formatted and " +
		"reformat not implemented"
	msgScmMountEmpty = "scm mount must be specified in config"
	msgScmBadDevList = "expecting one scm dcpm pmem device " +
		"per-server in config"
	msgScmDevEmpty          = "scm dcpm device list must contain path"
	msgScmClassNotSupported = "operation unsupported on scm class"
	msgIpmctlDiscoverFail   = "ipmctl module discovery"
	msgScmUpdateNotImpl     = "scm firmware update not supported"
)

type pmemDev struct {
	UUID     string
	Blockdev string
	Dev      string
	NumaNode int `json:"numa_node"`
}

func (pd *pmemDev) String() string {
	return fmt.Sprintf("%s, numa %d", pd.Blockdev, pd.NumaNode)
}

type runCmdFn func(string) (string, error)

type runCmdError struct {
	wrapped error
	stdout  string
}

func (rce *runCmdError) Error() string {
	if ee, ok := rce.wrapped.(*exec.ExitError); ok {
		return fmt.Sprintf("%s: stdout: %s; stderr: %s", ee.ProcessState,
			rce.stdout, ee.Stderr)
	}

	return fmt.Sprintf("%s: stdout: %s", rce.wrapped.Error(), rce.stdout)
}

// run wraps exec.Command().Output() to enable mocking of command output.
func run(cmd string) (string, error) {
	out, err := exec.Command("bash", "-c", cmd).Output()
	if err != nil {
		return "", &runCmdError{
			wrapped: err,
			stdout:  string(out),
		}
	}

	return string(out), nil
}

// scmStorage gives access to underlying storage interface implementation
// for accessing SCM devices (API) in addition to storage of device
// details.
//
// IpmCtl provides necessary methods to interact with Storage Class
// Memory modules through libipmctl via ipmctl bindings.
type scmStorage struct {
	ipmctl      ipmctl.IpmCtl  // ipmctl NVM API interface
	config      *Configuration // server configuration structure
	runCmd      runCmdFn
	modules     types.ScmModules
	state       scmState
	initialized bool
	formatted   bool
}

func (s *scmStorage) withRunCmd(runCmd runCmdFn) *scmStorage {
	s.runCmd = runCmd

	return s
}

// TODO: implement remaining methods for scmStorage
// func (s *scmStorage) Update(req interface{}) interface{} {return nil}
// func (s *scmStorage) BurnIn(req interface{}) (fioPath string, cmds []string, env string, err error) {
// return
// }

// Prep executes commands to configure SCM modules into AppDirect interleaved
// regions/sets hosting pmem kernel device namespaces.
//
// Presents of nonvolatile memory modules is assumed in this method and state
// is established based on presence and free capacity of regions.
//
// Actions based on state:
// * modules exist and no regions -> create all regions (needs reboot)
// * regions exist and free capacity -> create all namespaces, return created
// * regions exist but no free capacity -> no-op, return namespaces
//
// Command output from external tools will be returned.
func (s *scmStorage) Prep() (needsReboot bool, pmemDevs []pmemDev, err error) {
	if err := s.getState(); err != nil {
		return false, nil, errors.WithMessage(err, "establish scm state")
	}

	log.Debugf("scm in state %s\n", s.state)

	switch s.state {
	case scmStateNoRegions:
		// if successful, memory allocation change read on reboot
		if _, err = s.runCmd(cmdScmCreateRegions); err == nil {
			needsReboot = true
		}
	case scmStateFreeCapacity:
		pmemDevs, err = s.createNamespaces()
	case scmStateNoCapacity:
		pmemDevs, err = s.getNamespaces()
	case scmStateUnknown:
		err = errors.New("unknown scm state")
	}

	return
}

// PrepReset executes commands to remove namespaces and regions on SCM modules.
//
// Returns indication of whether a reboot is required alongside error.
func (s *scmStorage) PrepReset() (bool, error) {
	if err := s.getState(); err != nil {
		return false, err
	}

	switch s.state {
	case scmStateNoRegions:
		log.Info("SCM is already reset\n")
		return false, nil
	case scmStateUnknown:
		return false, errors.New("unknown scm state")
	}

	pmemDevs, err := s.getNamespaces()
	if err != nil {
		return false, err
	}

	for _, dev := range pmemDevs {
		if err := s.removeNamespace(dev.Dev); err != nil {
			return false, err
		}
	}

	log.Infof("resetting SCM memory allocations\n")
	if out, err := s.runCmd(cmdScmRemoveRegions); err != nil {
		log.Error(out)
		return false, err
	}

	return true, nil // memory allocation reset requires a reboot
}

func (s *scmStorage) removeNamespace(devName string) (err error) {
	log.Infof("removing SCM namespace, may take a few minutes...\n")

	_, err = s.runCmd(fmt.Sprintf(cmdScmDisableNamespace, devName))
	if err != nil {
		return
	}

	_, err = s.runCmd(fmt.Sprintf(cmdScmDestroyNamespace, devName))
	if err != nil {
		return
	}

	return
}

// getState establishes state of SCM regions and namespaces on local server.
func (s *scmStorage) getState() error {
	s.state = scmStateUnknown

	// TODO: discovery should provide SCM region details
	out, err := s.runCmd(cmdScmShowRegions)
	if err != nil {
		return err
	}

	log.Debugf("show region output: %s\n", out)

	if out == outScmNoRegions {
		s.state = scmStateNoRegions
		return nil
	}

	ok, err := hasFreeCapacity(out)
	if err != nil {
		return err
	}
	if ok {
		s.state = scmStateFreeCapacity
		return nil
	}
	s.state = scmStateNoCapacity

	return nil
}

// hasFreeCapacity takes output from ipmctl and checks for free capacity.
//
// external tool commands return:
// $ ipmctl show -d PersistentMemoryType,FreeCapacity -region
//
// ---ISetID=0x2aba7f4828ef2ccc---
//    PersistentMemoryType=AppDirect
//    FreeCapacity=3012.0 GiB
// ---ISetID=0x81187f4881f02ccc---
//    PersistentMemoryType=AppDirect
//    FreeCapacity=3012.0 GiB
//
// FIXME: implementation to be replaced by using libipmctl directly through bindings
func hasFreeCapacity(text string) (hasCapacity bool, err error) {
	lines := strings.Split(text, "\n")
	if len(lines) < 4 {
		return false, errors.Errorf("expecting at least 4 lines, got %d",
			len(lines))
	}

	for _, line := range lines {
		entry := strings.TrimSpace(line)

		kv := strings.Split(entry, "=")
		if len(kv) != 2 {
			continue
		}

		if kv[0] == "PersistentMemoryType" && kv[1] == "AppDirect" {
			hasCapacity = true
			continue
		}

		if kv[0] != "FreeCapacity" {
			continue
		}

		if hasCapacity && kv[1] != "0.0 GiB" {
			return
		}

		hasCapacity = false
	}

	return
}

func parsePmemDevs(jsonData string) (devs []pmemDev, err error) {
	// turn single entries into arrays
	if !strings.HasPrefix(jsonData, "[") {
		jsonData = "[" + jsonData + "]"
	}

	err = json.Unmarshal([]byte(jsonData), &devs)

	return
}

// createNamespaces runs create until no free capacity.
func (s *scmStorage) createNamespaces() ([]pmemDev, error) {
	devs := make([]pmemDev, 0)

	for {
		log.Infof("creating SCM namespace, may take a few minutes...\n")

		out, err := s.runCmd(cmdScmCreateNamespace)
		if err != nil {
			return nil, err
		}

		newDevs, err := parsePmemDevs(out)
		if err != nil {
			return nil, err
		}
		devs = append(devs, newDevs...)

		if err := s.getState(); err != nil {
			return nil, err
		}

		switch {
		case s.state == scmStateNoCapacity:
			return devs, nil
		case s.state != scmStateFreeCapacity:
			return nil, errors.Errorf("unexpected state: want %s, got %s",
				scmStateFreeCapacity.String(), s.state.String())
		}
	}
}

func (s *scmStorage) getNamespaces() (devs []pmemDev, err error) {
	out, err := s.runCmd(cmdScmListNamespaces)
	if err != nil {
		return nil, err
	}

	return parsePmemDevs(out)
}

// Setup implementation for scmStorage providing initial device discovery
func (s *scmStorage) Setup() error {
	return s.Discover()
}

// Teardown implementation for scmStorage
func (s *scmStorage) Teardown() error {
	s.initialized = false
	return nil
}

func loadModules(mms []ipmctl.DeviceDiscovery) (pbMms types.ScmModules) {
	for _, c := range mms {
		pbMms = append(
			pbMms,
			&pb.ScmModule{
				Loc: &pb.ScmModule_Location{
					Channel:    uint32(c.Channel_id),
					Channelpos: uint32(c.Channel_pos),
					Memctrlr:   uint32(c.Memory_controller_id),
					Socket:     uint32(c.Socket_id),
				},
				Physicalid: uint32(c.Physical_id),
				Capacity:   c.Capacity,
			})
	}
	return
}

// Discover method implementation for scmStorage
func (s *scmStorage) Discover() error {
	if s.initialized {
		return nil
	}

	mms, err := s.ipmctl.Discover()
	if err != nil {
		return errors.WithMessage(err, msgIpmctlDiscoverFail)
	}
	s.modules = loadModules(mms)
	s.initialized = true

	return nil
}

// clearMount unmounts then removes mount point.
//
// NOTE: requires elevated privileges
func (s *scmStorage) clearMount(mntPoint string) (err error) {
	if err = s.config.ext.unmount(mntPoint); err != nil {
		return
	}

	if err = s.config.ext.remove(mntPoint); err != nil {
		return
	}

	return
}

// reFormat wipes fs signatures and formats dev with ext4.
//
// NOTE: Requires elevated privileges and is a destructive operation, prompt
//       user for confirmation before running.
func (s *scmStorage) reFormat(devPath string) (err error) {
	log.Debugf("wiping all fs identifiers on device %s", devPath)

	if err = s.config.ext.runCommand(
		fmt.Sprintf("wipefs -a %s", devPath)); err != nil {

		return errors.WithMessage(err, "wipefs")
	}

	if err = s.config.ext.runCommand(
		fmt.Sprintf("mkfs.ext4 %s", devPath)); err != nil {

		return errors.WithMessage(err, "mkfs format")
	}

	return
}

func getMntParams(srv *IOServerConfig) (mntType string, dev string, opts string, err error) {
	switch srv.ScmClass {
	case scmDCPM:
		mntType = "ext4"
		opts = "dax"
		if len(srv.ScmList) != 1 {
			err = errors.New(msgScmBadDevList)
			break
		}

		dev = srv.ScmList[0]
		if dev == "" {
			err = errors.New(msgScmDevEmpty)
		}
	case scmRAM:
		dev = "tmpfs"
		mntType = "tmpfs"

		if srv.ScmSize >= 0 {
			opts = "size=" + strconv.Itoa(srv.ScmSize) + "g"
		}
	default:
		err = errors.New(string(srv.ScmClass) + ": " + msgScmClassNotSupported)
	}

	return
}

// makeMount creates a mount target directory and mounts device there.
//
// NOTE: requires elevated privileges
func (s *scmStorage) makeMount(devPath string, mntPoint string, mntType string,
	mntOpts string) (err error) {

	if err = s.config.ext.mkdir(mntPoint); err != nil {
		return
	}

	if err = s.config.ext.mount(devPath, mntPoint, mntType, uintptr(0), mntOpts); err != nil {
		return
	}

	return
}

// newMntRet creates and populates NVMe ctrlr result and logs error through
// newState.
func newMntRet(op string, mntPoint string, status pb.ResponseStatus, errMsg string,
	infoMsg string) *pb.ScmMountResult {

	return &pb.ScmMountResult{
		Mntpoint: mntPoint,
		State:    newState(status, errMsg, infoMsg, "scm mount "+op),
	}
}

// Format attempts to format (forcefully) SCM mounts on a given server
// as specified in config file and populates resp ScmMountResult.
func (s *scmStorage) Format(i int, results *(types.ScmMountResults)) {
	srv := s.config.Servers[i]
	mntPoint := srv.ScmMount
	log.Debugf("performing SCM device reset, format and mount")

	// wraps around addMret to provide format specific function, ignore infoMsg
	addMretFormat := func(status pb.ResponseStatus, errMsg string) {
		*results = append(*results,
			newMntRet("format", mntPoint, status, errMsg, ""))
	}

	if !s.initialized {
		addMretFormat(pb.ResponseStatus_CTRL_ERR_APP, msgScmNotInited)
		return
	}

	if s.formatted {
		addMretFormat(pb.ResponseStatus_CTRL_ERR_APP, msgScmAlreadyFormatted)
		return
	}

	if mntPoint == "" {
		addMretFormat(pb.ResponseStatus_CTRL_ERR_CONF, msgScmMountEmpty)
		return
	}

	mntType, devPath, mntOpts, err := getMntParams(srv)
	if err != nil {
		addMretFormat(pb.ResponseStatus_CTRL_ERR_CONF, err.Error())
		return
	}

	switch srv.ScmClass {
	case scmDCPM:
		if err := s.clearMount(mntPoint); err != nil {
			addMretFormat(pb.ResponseStatus_CTRL_ERR_APP, err.Error())
			return
		}

		log.Debugf("formatting scm device %s, should be quick!...", devPath)

		if err := s.reFormat(devPath); err != nil {
			addMretFormat(pb.ResponseStatus_CTRL_ERR_APP, err.Error())
			return
		}

		log.Debugf("scm format complete.\n")
	case scmRAM:
		if err := s.clearMount(mntPoint); err != nil {
			addMretFormat(pb.ResponseStatus_CTRL_ERR_APP, err.Error())
			return
		}

		log.Debugf("no scm_size specified in config for ram tmpfs")
	}

	log.Debugf("mounting scm device %s at %s (%s)...", devPath, mntPoint, mntType)

	if err := s.makeMount(devPath, mntPoint, mntType, mntOpts); err != nil {
		addMretFormat(pb.ResponseStatus_CTRL_ERR_APP, err.Error())
		return
	}

	log.Debugf("scm mount complete.\n")
	addMretFormat(pb.ResponseStatus_CTRL_SUCCESS, "")

	log.Debugf("SCM device reset, format and mount completed")
	s.formatted = true
}

// Update is currently a placeholder method stubbing SCM module fw update.
func (s *scmStorage) Update(
	i int, req *pb.UpdateScmReq, results *(types.ScmModuleResults)) {

	// respond with single result indicating no implementation
	*results = append(
		*results,
		&pb.ScmModuleResult{
			Loc: &pb.ScmModule_Location{},
			State: newState(pb.ResponseStatus_CTRL_NO_IMPL,
				msgScmUpdateNotImpl, "", "scm module update"),
		})
}

// newScmStorage creates a new instance of ScmStorage struct.
//
// NvmMgmt is the implementation of ipmctl interface in ipmctl
func newScmStorage(config *Configuration) *scmStorage {
	return &scmStorage{
		ipmctl: &ipmctl.NvmMgmt{},
		config: config,
		runCmd: run,
	}
}
