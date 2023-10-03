//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"context"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

var (
	lockCmpOpts = []cmp.Option{
		cmp.AllowUnexported(PoolLock{}),
		cmpopts.IgnoreUnexported(sync.Once{}),
	}
)

func makeLock(refCt, id, pool int32) *PoolLock {
	return &PoolLock{
		id:       uuid.MustParse(test.MockUUID(id)),
		poolUUID: uuid.MustParse(test.MockUUID(pool)),
		takenAt:  time.Now(),
		refCount: refCt,
	}
}

func TestRaft_getLockCtx(t *testing.T) {
	lock := makeLock(0, 1, 2)

	for name, tc := range map[string]struct {
		ctx    context.Context
		expErr error
	}{
		"nil context": {
			ctx:    nil,
			expErr: errors.New("nil context"),
		},
		"no lock in context": {
			ctx:    test.Context(t),
			expErr: errNoCtxLock,
		},
		"lock in context": {
			ctx: lock.InContext(test.Context(t)),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotLock, gotErr := getCtxLock(tc.ctx)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(lock, gotLock, lockCmpOpts...); diff != "" {
				t.Fatalf("unexpected lock (-want, +got):\n%s", diff)
			}
		})
	}
}

func TestRaft_PoolLock_Release(t *testing.T) {
	var released int32

	lock := makeLock(3, 1, 2)
	lock.release = func() { atomic.AddInt32(&released, 1) }
	lock.addRef()

	var wg sync.WaitGroup
	for i := 0; i < 12; i++ {
		wg.Add(1)
		// run in goroutines to trigger the race detector
		// on any unsafe shenanigans.
		go func() {
			lock.Release()
			wg.Done()
		}()
	}

	wg.Wait()
	test.AssertEqual(t, atomic.LoadInt32(&released), int32(1), "unexpected release count")
}

func TestRaft_PoolLock_InContext(t *testing.T) {
	lock1 := makeLock(0, 1, 2)
	lock2 := makeLock(0, 3, 4)

	for name, tc := range map[string]struct {
		parent      context.Context
		expLock     *PoolLock
		shouldPanic bool
	}{
		"nil parent": {
			parent:      nil,
			shouldPanic: true,
		},
		"parent contains different lock": {
			parent:      lock2.InContext(test.Context(t)),
			shouldPanic: true,
		},
		"parent contains same lock": {
			parent:  lock1.InContext(test.Context(t)),
			expLock: lock1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			defer func() {
				if r := recover(); r != nil {
					if !tc.shouldPanic {
						t.Fatalf("unexpected panic: %v", r)
					}
				}
			}()

			ctx := lock1.InContext(tc.parent)
			if tc.shouldPanic {
				t.Fatal("expected panic")
			}

			gotLock, err := getCtxLock(ctx)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expLock, gotLock, lockCmpOpts...); diff != "" {
				t.Fatalf("unexpected lock (-want, +got):\n%s", diff)
			}
		})
	}
}

func TestRaft_AddContextLock(t *testing.T) {
	lock1 := makeLock(0, 1, 2)
	lock2 := makeLock(0, 3, 4)

	for name, tc := range map[string]struct {
		parent    context.Context
		lock      *PoolLock
		expLock   *PoolLock
		expNewCtx bool
		expErr    error
	}{
		"nil parent": {
			parent: nil,
			lock:   lock1,
			expErr: errors.New("nil context"),
		},
		"nil lock": {
			parent: test.Context(t),
			expErr: errors.New("nil lock"),
		},
		"parent contains different lock": {
			parent: lock2.InContext(test.Context(t)),
			lock:   lock1,
			expErr: errors.New("contains another lock"),
		},
		"parent contains same lock": {
			parent:  lock1.InContext(test.Context(t)),
			lock:    lock1,
			expLock: lock1,
		},
		"new child context with lock": {
			parent:    test.Context(t),
			lock:      lock1,
			expLock:   lock1,
			expNewCtx: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx, gotErr := AddContextLock(tc.parent, tc.lock)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotLock, err := getCtxLock(ctx)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expLock, gotLock, lockCmpOpts...); diff != "" {
				t.Fatalf("unexpected lock (-want, +got):\n%s", diff)
			}

			if tc.expNewCtx && ctx == tc.parent {
				t.Fatal("expected new context")
			} else if !tc.expNewCtx && ctx != tc.parent {
				t.Fatal("expected same context")
			}
		})
	}
}

func TestRaft_poolLockMap_take(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	uuid0 := uuid.MustParse(test.MockUUID(1))
	uuid1 := uuid.MustParse(test.MockUUID(2))
	lock0 := &PoolLock{id: uuid1, poolUUID: uuid0}

	for name, tc := range map[string]struct {
		plm        *poolLockMap
		poolToLock uuid.UUID
		expErr     error
		expLock    *PoolLock
	}{
		"already locked": {
			plm: func() *poolLockMap {
				plm := &poolLockMap{log: log}
				plm.take(uuid0)
				return plm
			}(),
			poolToLock: uuid0,
			expErr:     errors.New("locked"),
		},
		"lock taken successfully": {
			poolToLock: uuid0,
			expLock:    lock0,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.plm == nil {
				tc.plm = &poolLockMap{log: log}
			}
			defer test.ShowBufferOnFailure(t, buf)

			gotLock, err := tc.plm.take(tc.poolToLock)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expLock.poolUUID, gotLock.poolUUID, "unexpected lock")
		})
	}
}

func TestRaft_poolLockMap_checkLock(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	uuid0 := uuid.MustParse(test.MockUUID(1))
	uuid1 := uuid.MustParse(test.MockUUID(2))
	lock0 := &PoolLock{id: uuid1, poolUUID: uuid0}
	lock1 := &PoolLock{id: uuid0, poolUUID: uuid0}

	for name, tc := range map[string]struct {
		plm       *poolLockMap
		checkLock *PoolLock
		expErr    error
	}{
		"nil lock": {
			expErr: errors.New("nil pool lock"),
		},
		"locked, same id": {
			plm: func() *poolLockMap {
				plm := &poolLockMap{log: log}
				plm.take(uuid0)
				plm.locks[uuid0].id = lock0.id
				return plm
			}(),
			checkLock: lock0,
		},
		"locked, different id": {
			plm: func() *poolLockMap {
				plm := &poolLockMap{log: log}
				plm.take(uuid0)
				plm.locks[uuid0].id = lock1.id
				return plm
			}(),
			checkLock: lock0,
			expErr:    errors.New("locked"),
		},
		"not locked": {
			checkLock: lock0,
			expErr:    errors.New("not found"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.plm == nil {
				tc.plm = &poolLockMap{log: log}
			}
			defer test.ShowBufferOnFailure(t, buf)

			checkErr := tc.plm.checkLock(tc.checkLock)
			test.CmpErr(t, tc.expErr, checkErr)
		})
	}
}
