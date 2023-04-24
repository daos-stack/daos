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
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

type (
	// PoolLock represents a lock on a pool.
	// These locks are reference counted to make
	// them reentrant. Note that the lock release
	// closure will only ever be called once, no
	// matter how many times Release() is called.
	PoolLock struct {
		id       uuid.UUID
		poolUUID uuid.UUID
		takenAt  time.Time
		refCount int32
		relOnce  sync.Once
		release  func()
	}

	// poolLockMap is a map of pool UUIDs to pool locks.
	// With this implementation, there may only be one
	// lock on a pool at a time in order to avoid concurrent
	// operations by multiple gRPC handlers on the same pool.
	// NB: This structure is not backed by raft, and is
	// intended to be local to the current MS leader.
	poolLockMap struct {
		sync.RWMutex
		locks map[uuid.UUID]*PoolLock
		log   logging.DebugLogger
	}

	ctxKey string
)

const (
	poolLockKey ctxKey = "poolLock"
)

var (
	errNoCtxLock = errors.New("no pool lock in context")
)

// getCtxLock returns the pool lock from the supplied context, if
// one is present.
func getCtxLock(ctx context.Context) (*PoolLock, error) {
	if ctx == nil {
		return nil, errors.New("nil context in getCtxLock()")
	}

	lock, ok := ctx.Value(poolLockKey).(*PoolLock)
	if !ok {
		return nil, errNoCtxLock
	}

	return lock, nil
}

// AddContextLock adds a pool lock to the supplied context.
func AddContextLock(parent context.Context, lock *PoolLock) (context.Context, error) {
	if parent == nil {
		return nil, errors.New("nil context in AddContextLock()")
	}
	if lock == nil {
		return nil, errors.New("nil lock in AddContextLock()")
	}

	cl, err := getCtxLock(parent)
	if err != nil {
		if err != errNoCtxLock {
			return nil, err
		}
		// If the parent context doesn't already have a lock,
		// add the supplied lock to a new child context.
		return context.WithValue(parent, poolLockKey, lock), nil
	}

	// Otherwise, if the parent context already has a lock,
	// verify that it is the same as the supplied lock and
	// return an error if not.
	if cl.id != lock.id {
		return nil, errors.Errorf("parent context contains another lock for pool %s", cl.poolUUID)
	}

	// If the parent context already has the supplied lock,
	// just return the parent context.
	return parent, nil
}

// InContext returns a new child context with the lock added
// as a value. NB: It is the caller's responsibility to ensure
// that the parent context is valid and does not already contain
// a different pool lock. For a more robust implementation, see
// the AddContextLock() function.
func (pl *PoolLock) InContext(parent context.Context) context.Context {
	ctx, err := AddContextLock(parent, pl)
	if err != nil {
		panic(err)
	}
	return ctx
}

func (pl *PoolLock) addRef() {
	atomic.AddInt32(&pl.refCount, 1)
}

func (pl *PoolLock) decRef() {
	atomic.AddInt32(&pl.refCount, -1)
}

func (pl *PoolLock) getRef() int32 {
	return atomic.LoadInt32(&pl.refCount)
}

// Release releases the lock on the pool when the reference
// count reaches zero.
func (pl *PoolLock) Release() {
	pl.decRef()
	if pl.getRef() > 0 {
		return
	}
	pl.relOnce.Do(pl.release)
}

// take returns a new pool lock for the supplied pool UUID
// if the pool is not already locked, otherwise it returns
// an error.
func (plm *poolLockMap) take(poolUUID uuid.UUID) (*PoolLock, error) {
	if poolUUID == uuid.Nil {
		return nil, errors.New("nil pool UUID")
	}

	plm.Lock()
	defer plm.Unlock()

	if plm.locks == nil {
		plm.locks = make(map[uuid.UUID]*PoolLock)
	}

	if lock, exists := plm.locks[poolUUID]; exists {
		return nil, system.FaultPoolLocked(poolUUID, lock.id, lock.takenAt)
	}

	lock := &PoolLock{
		id:       uuid.New(),
		poolUUID: poolUUID,
		takenAt:  time.Now(),
		release:  func() { plm.release(poolUUID) },
	}
	lock.addRef()
	plm.locks[poolUUID] = lock

	plm.log.Debugf("%s: lock taken (id: %s)", dbgUuidStr(poolUUID), dbgUuidStr(lock.id))
	return lock, nil
}

// release releases the lock on the pool with the supplied UUID.
func (plm *poolLockMap) release(poolUUID uuid.UUID) {
	plm.Lock()
	defer plm.Unlock()

	plm.log.Debugf("%s: lock released", dbgUuidStr(poolUUID))
	delete(plm.locks, poolUUID)
}

// checkLockCtx is a helper to extract the pool lock from the
// supplied context before sending it to checkLock().
func (plm *poolLockMap) checkLockCtx(ctx context.Context) error {
	lock, err := getCtxLock(ctx)
	if err != nil {
		return err
	}

	return plm.checkLock(lock)
}

// checkLock checks that the supplied lock is still valid.
func (plm *poolLockMap) checkLock(lock *PoolLock) error {
	if lock == nil {
		return errors.New("nil pool lock in checkLock()")
	}
	plm.RLock()
	defer plm.RUnlock()

	// Verify that the supplied lock matches the stored lock. This
	// is a belt-and-suspenders check because in theory the logic
	// in take() should prevent this from ever happening.
	if pl, exists := plm.locks[lock.poolUUID]; exists {
		if lock.id != pl.id {
			return system.FaultPoolLocked(lock.poolUUID, pl.id, pl.takenAt)
		}
	} else {
		return errors.Errorf("pool %s: lock not found", lock.poolUUID)
	}

	return nil
}
