//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package telemetry

/*
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
*/
import "C"

import (
	"time"

	"github.com/pkg/errors"
)

type shmStWrp struct {
	id C.int
	ds C.struct_shmid_ds
}

// Size returns the size of segment in bytes.
func (s *shmStWrp) Size() int {
	return int(s.ds.shm_segsz)
}

// Atime returns the time of last shmat(2).
func (s *shmStWrp) Atime() time.Time {
	return time.Unix(int64(s.ds.shm_atime), 0)
}

// Dtime returns the time of last shmdt(2).
func (s *shmStWrp) Dtime() time.Time {
	return time.Unix(int64(s.ds.shm_dtime), 0)
}

// Ctime returns the time of last shmctl(2) or creation time.
func (s *shmStWrp) Ctime() time.Time {
	return time.Unix(int64(s.ds.shm_ctime), 0)
}

// Cpid returns the creator pid.
func (s *shmStWrp) Cpid() int {
	return int(s.ds.shm_cpid)
}

// Lpid returns the last shmat(2)/shmdt(2) pid.
func (s *shmStWrp) Lpid() int {
	return int(s.ds.shm_lpid)
}

// Nattach returns the number of attached processes.
func (s *shmStWrp) Nattach() int {
	return int(s.ds.shm_nattch)
}

// C returns the C struct.
func (s *shmStWrp) C() *C.struct_shmid_ds {
	return &s.ds
}

func shmStat(id C.int) (*shmStWrp, error) {
	st := shmStWrp{
		id: id,
	}
	rc, err := C.shmctl(id, C.IPC_STAT, &st.ds)
	if rc != 0 {
		return nil, errors.Wrapf(err, "shmctl(IPC_STAT, %d)", id)
	}

	return &st, nil
}

func shmStatKey(key C.key_t) (*shmStWrp, error) {
	id, err := C.shmget(key, 0, 0)
	if err != nil {
		return nil, errors.Wrapf(err, "shmget(%d, 0, 0)", key)
	}

	return shmStat(id)
}

func shmChown(key C.key_t, uid C.uid_t, gid C.gid_t) error {
	st, err := shmStatKey(key)
	if err != nil {
		return err
	}

	st.ds.shm_perm.gid = gid
	st.ds.shm_perm.uid = uid

	rc, err := C.shmctl(st.id, C.IPC_SET, st.C())
	if rc != 0 {
		return errors.Wrapf(err, "shmctl(IPC_SET, %d)", st.id)
	}

	return nil
}
