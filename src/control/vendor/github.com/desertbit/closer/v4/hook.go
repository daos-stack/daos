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

package closer

import (
	"sync"
)

type H interface {
	// OnClose same as OnCloseWithErr but without error.
	OnClose(f ...func())

	// OnClosing same as OnClosingWithErr but without error.
	OnClosing(f ...func())

	// OnCloseWithErr adds the given functions to the closer.
	// Their errors are joined with the closer's other errors.
	// Close functions are called in LIFO order.
	// See Close() for their position in the closing order.
	OnCloseWithErr(f ...func() error)

	// OnClosingWithErr adds the given functions to the closer.
	// Their errors are joined with the closer's other errors.
	// Closing functions are called in LIFO order.
	// It is guaranteed that all closing funcs within this hook scope are executed before
	// any close funcs.
	// See Close() for their position in the closing order.
	OnClosingWithErr(f ...func() error)
}

// Hook can be used to register closer hooks.
// Do not use the passed hook H after the function goes out-of-scope.
// Hook gaurenetees, that the newly registered hooks within this context will be called after the function scope.
// This also applies, if the closer is already closed.
// Use this call to register all hooks within a constructor function.
func Hook(cl Closer, f func(H)) {
	// Early close not possible. We can not return an error.
	// Ensure to always call the hook function, otherwise the caller must check for nil values...
	h := newHook(cl)
	f(h)
	h.commit()
}

// HookWithErr same as Hook, but returns the inner error.
// If the closer is already closed, then the inner function will not run and ErrClosed will be returned.
func HookWithErr(cl Closer, f func(H) error) error {
	return Block(cl, func() error {
		h := newHook(cl)

		err := f(h)
		if err != nil {
			h.execFuncs()   // Cleanup.
			cl.AsyncClose() // Always close the closer on error.
			return err
		}

		h.commit()
		return nil
	})
}

type hook struct {
	cl *closer

	mx           sync.Mutex
	closeFuncs   []func() error
	closingFuncs []func() error
}

func newHook(cl Closer) *hook {
	return &hook{
		cl: toInternal(cl),
	}
}

// Implements the Hook interface.
func (h *hook) OnClose(f ...func()) {
	h.mx.Lock()
	defer h.mx.Unlock()

	for _, cb := range f {
		cbCopy := cb
		h.closeFuncs = append(h.closeFuncs, func() error {
			cbCopy()
			return nil
		})
	}
}

// Implements the Hook interface.
func (h *hook) OnClosing(f ...func()) {
	h.mx.Lock()
	defer h.mx.Unlock()

	for _, cb := range f {
		cbCopy := cb
		h.closingFuncs = append(h.closingFuncs, func() error {
			cbCopy()
			return nil
		})
	}
}

// Implements the Hook interface.
func (h *hook) OnCloseWithErr(f ...func() error) {
	h.mx.Lock()
	h.closeFuncs = append(h.closeFuncs, f...)
	h.mx.Unlock()
}

// Implements the Hook interface.
func (h *hook) OnClosingWithErr(f ...func() error) {
	h.mx.Lock()
	h.closingFuncs = append(h.closingFuncs, f...)
	h.mx.Unlock()
}

func (h *hook) commit() {
	c := h.cl

	// Hint: hook mx lock not required, because commit will be called after the hook function.
	c.mx.Lock()

	// If the closer is already closing/closed, then cleanup.
	if c.IsClosing() {
		c.mx.Unlock()
		h.execFuncs()
		return
	}

	c.closeFuncs = append(c.closeFuncs, h.closeFuncs...)
	c.closingFuncs = append(c.closingFuncs, h.closingFuncs...)
	c.mx.Unlock()
}

func (h *hook) execFuncs() {
	// Execute all close funcs of this hook in LIFO order.
	// Mimic the closer behaviour.
	for i := len(h.closingFuncs) - 1; i >= 0; i-- {
		_ = h.closingFuncs[i]()
	}
	for i := len(h.closeFuncs) - 1; i >= 0; i-- {
		_ = h.closeFuncs[i]()
	}
}
