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
	"context"
	"time"
)

type ctx struct {
	doneChan <-chan struct{}
}

func (c *ctx) Deadline() (deadline time.Time, ok bool) {
	return
}

func (c *ctx) Done() <-chan struct{} {
	return c.doneChan
}

func (c *ctx) Err() error {
	select {
	case <-c.doneChan:
		return ErrClosed
	default:
		return nil
	}
}

func (c *ctx) Value(key any) any {
	return nil
}

// Context returns a context.Context, which is done
// as soon as the closer is closing.
// The retuned error will be ErrClosed.
func Context(cl Closer) context.Context {
	return &ctx{
		doneChan: cl.ClosingChan(), // We will use the closing chan, because otherwise deadlocks are possible.
	}
}

// Context returns a context.Context, which is cancelled
// as soon as the closer is closing.
// The returned cancel func should be called as soon as the
// context is no longer needed, to free resources.
func ContextWithCancel(cl Closer) (context.Context, context.CancelFunc) {
	return context.WithCancel(Context(cl))
}

// OnContextDoneClose closes the closer if the context is done.
func OnContextDoneClose(ctx context.Context, cl Closer) {
	go func() {
		select {
		case <-cl.ClosingChan():
		case <-ctx.Done():
			cl.Close()
		}
	}()
}
