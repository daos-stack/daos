/*
 * closer - A simple, thread-safe closer
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Roland Singer <roland.singer[at]desertbit.com>
 * Copyright (c) 2019 Sebastian Borchers <sebastian[at]desertbit.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// Package closer offers a simple, thread-safe closer.
//
// It allows to build up a tree of closing relationships, where you typically
// start with a root closer that branches into different children and
// children's children. When a parent closer spawns a child closer, the child
// either has a one-way or two-way connection to its parent. One-way children
// are closed when their parent closes. In addition, two-way children also close
// their parent, if they are closed themselves.
//
// A closer is also useful to ensure that certain dependencies, such as network
// connections, are reliably taken down, once the closer closes.
// In addition, a closer can be concurrently closed many times, without closing
// more than once, but still returning the errors to every caller.
//
// This allows to represent complex closing relationships and helps avoiding
// leaking goroutines, gracefully shutting down, etc.
package closer

import (
	"context"
	"errors"
	"fmt"
	"log"
	"os"
	"sync"
	"time"
)

// ErrClosed is a generic error that indicates a resource has been closed.
var ErrClosed = errors.New("closed")

//#############//
//### Types ###//
//#############//

// CloseFunc defines the general close function.
type CloseFunc func() error

//#################//
//### Interface ###//
//#################//

// A Closer is a thread-safe helper for common close actions.
type Closer interface {
	// Close closes this closer in a thread-safe manner.
	//
	// Implements the io.Closer interface.
	//
	// This method always returns the close error,
	// regardless of how often it gets called.
	//
	// The closing order looks like this:
	// 1: the closing chan is closed.
	// 2: the OnClosing funcs are executed.
	// 3: each of the closer's children is closed.
	// 4: it waits for the wait group.
	// 5: the OnClose funcs are executed.
	// 6: the closed chan is closed.
	// 7: the parent is closed, if it has one.
	//
	// Close blocks, until step 6 of the closing order
	// has been finished. A potential parent gets
	// closed concurrently in a new goroutine.
	//
	// The returned error contains the joined errors of all closers that were part of
	// the blocking closing order of this closer.
	// This means that two-way closers do not report their parents' errors.
	Close() error

	// Close_ is a convenience version of Close(), for use in defer
	// where the error is not of interest.
	Close_()

	// CloseWithErr closes the closer and appends the given error to its joined error.
	CloseWithErr(err error)

	// CloseWithErrAndDone performs the same operation as CloseWithErr(), but decrements
	// the closer's wait group by one beforehand.
	// Attention: Calling this without first calling CloserAddWait results in a panic.
	CloseWithErrAndDone(err error)

	// CloseAndDone performs the same operation as Close(), but decrements
	// the closer's wait group by one beforehand.
	// Attention: Calling this without first calling CloserAddWait results in a panic.
	CloseAndDone() error

	// CloseAndDone_ is a convenience version of CloseAndDone(), for use in
	// defer where the error is not of interest.
	CloseAndDone_()

	// CloserAddWait adds the given delta to the closer's
	// wait group. Useful to wait for routines associated
	// with this closer to gracefully shutdown.
	// See Close() for the position in the closing order.
	CloserAddWait(delta int)

	// CloserDone decrements the closer's wait group by one.
	// Attention: Calling this without first calling CloserAddWait results in a panic.
	CloserDone()

	// CloserOneWay creates a new child closer that has a one-way relationship
	// with the current closer. This means that the child is closed whenever
	// the parent closes, but not vice versa.
	// See Close() for the position in the closing order.
	CloserOneWay() Closer

	// CloserTwoWay creates a new child closer that has a two-way relationship
	// with the current closer. This means that the child is closed whenever
	// the parent closes and vice versa.
	// See Close() for the position in the closing order.
	CloserTwoWay() Closer

	// Context returns a context.Context, which is cancelled
	// as soon as the closer is closing.
	// The returned cancel func should be called as soon as the
	// context is no longer needed, to free resources.
	Context() (context.Context, context.CancelFunc)

	// CloseOnContextDone closes the closer if the context is done.
	CloseOnContextDone(context.Context)

	// ClosingChan returns a channel, which is closed as
	// soon as the closer is about to close.
	// Remains closed, once ClosedChan() has also been closed.
	// See Close() for the position in the closing order.
	ClosingChan() <-chan struct{}

	// ClosedChan returns a channel, which is closed as
	// soon as the closer is completely closed.
	// See Close() for the position in the closing order.
	ClosedChan() <-chan struct{}

	// IsClosing returns a boolean indicating
	// whether this instance is about to close.
	// Also returns true, if IsClosed() returns true.
	IsClosing() bool

	// IsClosed returns a boolean indicating
	// whether this instance has been closed completely.
	IsClosed() bool

	// OnClose adds the given CloseFuncs to the closer.
	// Their errors are joined with the closer's other errors.
	// Close functions are called in LIFO order.
	// See Close() for their position in the closing order.
	OnClose(f ...CloseFunc)

	// OnClosing adds the given CloseFuncs to the closer.
	// Their errors are joined with the closer's other errors.
	// Closing functions are called in LIFO order.
	// It is guaranteed that all closing funcs are executed before
	// any close funcs.
	// See Close() for their position in the closing order.
	OnClosing(f ...CloseFunc)

	// CloserError returns the joined error of this closer once it has fully closed.
	// If there was no error or the closer is not yet closed, nil is returned.
	CloserError() error

	// CloserWait waits for the closer to close and returns the CloserError if present.
	// Use the context to cancel the blocking wait.
	CloserWait(ctx context.Context) error

	// CloserWaitChan extends CloserWait by returning an error channel.
	// If the context is canceled, the context error will be send over the channel.
	CloserWaitChan(ctx context.Context) <-chan error

	// BlockCloser ensures that during the function execution, the closer will not reach the
	// closed state. This is handled by calling CloserAddWait.
	// This call will return ErrClosed, if the closer is already closed.
	// This method can be used to free C memory during OnClose and ensures,
	// that pointers are not used after beeing freed.
	BlockCloser(f func() error) error

	// RunCloserRoutine starts a closer goroutine:
	// - call CloserAddWait
	// - start a new goroutine
	// - wait for the routine function to return
	// - handle the error and close the closer by calling CloseWithErrAndDone
	RunCloserRoutine(f func() error)
}

//######################//
//### Implementation ###//
//######################//

const (
	minChildrenCap = 100
)

// The closer type is this package's implementation of the Closer interface.
type closer struct {
	// An unbuffered channel that expresses whether the
	// closer is about to close.
	// The channel itself gets closed to represent the closing
	// of the closer, which leads to reads off of it to succeed.
	closingChan chan struct{}
	// An unbuffered channel that expresses whether the
	// closer has been completely closed.
	// The channel itself gets closed to represent the closing
	// of the closer, which leads to reads off of it to succeed.
	closedChan chan struct{}
	// The error collected by executing the Close() func
	// and combining all encountered errors from the close funcs as joined error.
	closeErr error

	// Synchronises the access to the following properties.
	mx sync.Mutex
	// The close funcs that are executed when this closer closes.
	closeFuncs []CloseFunc
	// The closing funcs that are executed when this closer closes.
	closingFuncs []CloseFunc
	// The parent of this closer. May be nil.
	parent *closer
	// The closer children that this closer spawned.
	children []*closer
	// Used to wait for external dependencies of the closer
	// before the Close() method actually returns.
	// Use a custom implementation, because the sync.WaitGroup Wait() method is not thread-safe.
	waitCond  *sync.Cond
	waitCount int64

	// A flag that indicates whether this closer is a two-way closer.
	// In comparison to a standard one-way closer, which closes when
	// its parent closes, a two-way closer closes also its parent, when
	// it itself gets closed.
	twoWay bool

	// The index of this closer in its parent's children slice.
	// Needed to efficiently remove the closer from its parent.
	parentIndex int
}

// New creates a new closer.
func New() Closer {
	return newCloser(3)
}

// Implements the Closer interface.
func (c *closer) Close() error {
	// Close the closing channel to signal that this closer is about to close now.
	// Do this in a locked context and release as soon as the channel is closed.
	// If another close call is handling this context, then wait for it to exit before returning the error.
	c.mx.Lock()
	if c.IsClosing() {
		c.mx.Unlock()
		<-c.closedChan
		return c.closeErr
	}
	close(c.closingChan)
	// Copy the internal variables to local variables. Otherwise direct access could cause a race.
	var (
		closingFuncs = c.closingFuncs
		closeFuncs   = c.closeFuncs
		children     = c.children
	)
	c.closingFuncs = nil
	c.closeFuncs = nil
	c.children = nil
	c.mx.Unlock()

	// We are in an unlocked state. Do not use c.closeErr directly.
	var closeErrors error

	// Execute all closing funcs of this closer in LIFO order.
	for i := len(closingFuncs) - 1; i >= 0; i-- {
		closeErrors = errors.Join(closeErrors, closingFuncs[i]())
	}

	// Close all children and join their errors.
	for _, child := range children {
		closeErrors = errors.Join(closeErrors, child.Close())
	}

	// Wait, until all dependencies of this closer have closed.
	c.mx.Lock()
	for c.waitCount > 0 {
		c.waitCond.Wait()
	}
	c.mx.Unlock()

	// Execute all close funcs of this closer in LIFO order.
	for i := len(closeFuncs) - 1; i >= 0; i-- {
		closeErrors = errors.Join(closeErrors, closeFuncs[i]())
	}

	// Close the closed channel to signal that this closer is closed now.
	// Finally merge the errors. Do this in a locked context.
	c.mx.Lock()
	c.closeErr = errors.Join(c.closeErr, closeErrors)
	close(c.closedChan)
	c.mx.Unlock()

	// Close the parent now as well, if this is a two way closer.
	// Otherwise, the closer must remove its reference from its parent's children
	// to prevent a leak.
	// Only perform these actions, if the parent is not closing already!
	if c.parent != nil && !c.parent.IsClosing() {
		if c.twoWay {
			// Do not wait for the parent close. This may cause a dead-lock.
			// Traversing up the closer tree does not require that the children wait for their parents.
			go c.parent.Close_()
		} else {
			c.parent.removeChild(c)
		}
	}

	return c.closeErr
}

// Implements the Closer interface.
func (c *closer) Close_() {
	_ = c.Close()
}

// Implements the Closer interface.
func (c *closer) CloseWithErr(err error) {
	c.addError(err)
	c.Close_()
}

// Implements the Closer interface.
func (c *closer) CloseWithErrAndDone(err error) {
	c.addError(err)
	c.CloseAndDone_()
}

// Implements the Closer interface.
func (c *closer) CloseAndDone() error {
	c.CloserDone()
	return c.Close()
}

// Implements the Closer interface.
func (c *closer) CloseAndDone_() {
	_ = c.CloseAndDone()
}

// Implements the Closer interface.
func (c *closer) CloserAddWait(delta int) {
	c.closerAddWait(delta, true)
}

func (c *closer) closerAddWait(delta int, logEnabled bool) {
	c.mx.Lock()
	defer c.mx.Unlock()

	c.waitCount += int64(delta)

	if logEnabled && c.IsClosing() {
		// Print a debug stacktrace if build with debugging mode.
		if debugEnabled {
			// Use fmt instead of log for additional new line printing.
			fmt.Fprintf(os.Stderr, "\nDEBUG: CloserAddWait called during closing state:\n%s\n\n", stacktrace(3))
		} else {
			log.Println("Warning: CloserAddWait called during closing state")
		}
	}
}

// Implements the Closer interface.
func (c *closer) CloserDone() {
	c.mx.Lock()
	defer c.mx.Unlock()

	c.waitCount--
	c.waitCond.Broadcast()

	if c.waitCount < 0 {
		panic("CloserDone: negative wait counter")
	}
}

// Implements the Closer interface.
func (c *closer) CloserOneWay() Closer {
	return c.addChild(false)
}

// Implements the Closer interface.
func (c *closer) CloserTwoWay() Closer {
	return c.addChild(true)
}

// Implements the Closer interface.
func (c *closer) Context() (context.Context, context.CancelFunc) {
	ctx, cancel := context.WithCancel(context.Background())

	go func() {
		select {
		case <-c.closingChan:
			cancel()
		case <-ctx.Done():
		}
	}()

	return ctx, cancel
}

// Implements the Closer interface.
func (c *closer) CloseOnContextDone(ctx context.Context) {
	go func() {
		select {
		case <-c.closingChan:
		case <-ctx.Done():
			c.Close_()
		}
	}()
}

// Implements the Closer interface.
func (c *closer) ClosingChan() <-chan struct{} {
	return c.closingChan
}

// Implements the Closer interface.
func (c *closer) ClosedChan() <-chan struct{} {
	return c.closedChan
}

// Implements the Closer interface.
func (c *closer) IsClosing() bool {
	select {
	case <-c.closingChan:
		return true
	default:
		return false
	}
}

// Implements the Closer interface.
func (c *closer) IsClosed() bool {
	select {
	case <-c.closedChan:
		return true
	default:
		return false
	}
}

// Implements the Closer interface.
func (c *closer) OnClose(f ...CloseFunc) {
	c.mx.Lock()
	defer c.mx.Unlock()

	// FIXME: we can not fix this without breaking the API.
	/* if c.IsClosing() {
	        ...
			return
		} */

	c.closeFuncs = append(c.closeFuncs, f...)
}

// Implements the Closer interface.
func (c *closer) OnClosing(f ...CloseFunc) {
	c.mx.Lock()
	defer c.mx.Unlock()

	// FIXME: we can not fix this without breaking the API.
	/* if c.IsClosing() {
	        ...
			return
		} */

	c.closingFuncs = append(c.closingFuncs, f...)
}

// Implements the Closer interface.
func (c *closer) CloserError() (err error) {
	if c.IsClosed() {
		// No need for mutex lock since the closeErr is not modified
		// after the closer has closed.
		err = c.closeErr
	}
	return
}

// Implements the Closer interface.
func (c *closer) CloserWait(ctx context.Context) error {
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-c.ClosedChan():
		return c.CloserError()
	}
}

// Implements the Closer interface.
func (c *closer) CloserWaitChan(ctx context.Context) <-chan error {
	waitChan := make(chan error, 1)
	go func() {
		waitChan <- c.CloserWait(ctx)
	}()
	return waitChan
}

// Implements the Closer interface.
func (c *closer) BlockCloser(f func() error) error {
	var trace string
	if debugEnabled {
		trace = stacktrace(2)
	}

	c.closerAddWait(1, false)

	return func() error {
		defer c.CloserDone()

		// CloserAddWait will also add to a closed closer. Ensure we are not in a closing state.
		if c.IsClosing() {
			return ErrClosed
		}

		// Print a debug stacktrace if build with debugging mode.
		if debugEnabled {
			doneChan := make(chan struct{})
			go func() {
				<-c.closingChan

				t := time.NewTimer(debugLogAfterTimeout)
				defer t.Stop()

				select {
				case <-doneChan:
					return
				case <-t.C:
					// Use fmt instead of log for additional new line printing.
					fmt.Fprintf(os.Stderr, "\nDEBUG: BlockCloser takes longer than expected to close:\n%s\n\n", trace)
				}
			}()
			defer close(doneChan)
		}

		return f()
	}()
}

// Implements the Closer interface.
func (c *closer) RunCloserRoutine(f func() error) {
	var trace string
	if debugEnabled {
		trace = stacktrace(2)
	}

	c.closerAddWait(1, false)
	go func() {
		// CloserAddWait will also add to a closed closer. Ensure we are not in a closing state.
		if c.IsClosing() {
			c.CloserDone()
			return
		}

		// Print a debug stacktrace if build with debugging mode.
		if debugEnabled {
			doneChan := make(chan struct{})
			go func() {
				<-c.closingChan

				t := time.NewTimer(debugLogAfterTimeout)
				defer t.Stop()

				select {
				case <-doneChan:
					return
				case <-t.C:
					// Use fmt instead of log for additional new line printing.
					fmt.Fprintf(os.Stderr, "\nDEBUG: RunCloserRoutine takes longer than expected to close:\n%s\n\n", trace)
				}
			}()
			defer close(doneChan)
		}

		c.CloseWithErrAndDone(f())
	}()
}

//###############//
//### Private ###//
//###############//

// newCloser creates a new closer with the given close funcs.
func newCloser(debugSkipStacktrace int) *closer {
	c := &closer{
		closingChan: make(chan struct{}),
		closedChan:  make(chan struct{}),
	}
	c.waitCond = sync.NewCond(&c.mx)

	// Print a debug stacktrace if build with debugging mode.
	if debugEnabled {
		trace := stacktrace(debugSkipStacktrace)
		go func() {
			<-c.closingChan

			t := time.NewTimer(debugLogAfterTimeout)
			defer t.Stop()

			select {
			case <-c.closedChan:
				return
			case <-t.C:
				// Use fmt instead of log for additional new line printing.
				fmt.Fprintf(os.Stderr, "\nDEBUG: Closer takes longer than expected to close:\n%s\n\n", trace)
			}
		}()
	}

	return c
}

func (c *closer) addError(err error) {
	c.mx.Lock()
	defer c.mx.Unlock()

	// If the closer is already closed, then don't add errors.
	if c.IsClosed() {
		return
	}

	// Join the error.
	c.closeErr = errors.Join(c.closeErr, err)
}

// addChild creates a new closer and adds it as either
// a one-way or two-way child to this closer.
func (c *closer) addChild(twoWay bool) *closer {
	// Create a new closer and set the current closer as its parent.
	// Also set the twoWay flag.
	child := newCloser(4)
	child.parent = c
	child.twoWay = twoWay

	c.mx.Lock()
	defer c.mx.Unlock()

	// FIXME: because we can not fix OnClose & OnClosing, we must delay the child close.
	// The onClose & OnClosing callbacks are mostly created during construction phases. Ensure they are added before we close.
	// Close the new closer if the parent is already closed.
	if c.IsClosing() {
		go func() {
			time.Sleep(3 * time.Second)
			child.Close_()
		}()
		return child
	}

	// Add the closer to the current closer's children.
	child.parentIndex = len(c.children)
	c.children = append(c.children, child)

	return child
}

// removeChild removes the given child from this closer's children.
// If the child can not be found, this is a no-op.
func (c *closer) removeChild(child *closer) {
	c.mx.Lock()
	defer c.mx.Unlock()

	last := len(c.children) - 1
	if last < 0 {
		return
	}

	c.children[last].parentIndex = child.parentIndex
	c.children[child.parentIndex] = c.children[last]
	c.children[last] = nil
	c.children = c.children[:last]

	// Prevent endless growth.
	// If the capacity is bigger than our min value and
	// four times larger than the length, shrink it by half.
	cp := cap(c.children)
	le := len(c.children)
	if cp > minChildrenCap && cp > 4*le {
		children := make([]*closer, le, le*2)
		copy(children, c.children)
		c.children = children
	}
}
