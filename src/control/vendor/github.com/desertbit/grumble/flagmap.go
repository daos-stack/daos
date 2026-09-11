/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Roland Singer [roland.singer@deserbit.com]
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

package grumble

import (
	"fmt"
	"time"
)

// FlagMapItem holds the specific flag data.
type FlagMapItem struct {
	Value     interface{}
	IsDefault bool
}

// FlagMap holds all the parsed flag values.
type FlagMap map[string]*FlagMapItem

// copyMissingValues adds all missing values to the flags map.
func (f FlagMap) copyMissingValues(m FlagMap, copyDefault bool) {
	for k, v := range m {
		if _, ok := f[k]; !ok {
			if !copyDefault && v.IsDefault {
				continue
			}
			f[k] = v
		}
	}
}

// String returns the given flag value as string.
// Panics if not present. Flags must be registered.
func (f FlagMap) String(long string) string {
	s, ok := f.flagValOrPanic(long).(string)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to string", long))
	}
	return s
}

// StringList returns the given flag value as string slice.
// Panics if not present. Flags must be registered.
func (f FlagMap) StringList(long string) []string {
	i := f[long]
	if i == nil {
		panic(fmt.Errorf("missing flag value: flag '%s' not registered", long))
	}
	s := make([]string, len(i.Value.([]interface{})))
	var ok bool
	for k, v := range i.Value.([]interface{}) {
		s[k], ok = v.(string)
		if !ok {
			panic(fmt.Errorf("failed to assert flag '%s' to string", long))
		}
	}
	return s
}

// Bool returns the given flag value as boolean.
// Panics if not present. Flags must be registered.
func (f FlagMap) Bool(long string) bool {
	b, ok := f.flagValOrPanic(long).(bool)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to bool", long))
	}
	return b
}

// Int returns the given flag value as int.
// Panics if not present. Flags must be registered.
func (f FlagMap) Int(long string) int {
	v, ok := f.flagValOrPanic(long).(int)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to int", long))
	}
	return v
}

// Int8 returns the given flag value as int8.
// Panics if not present. Flags must be registered.
func (f FlagMap) Int8(long string) int8 {
	v, ok := f.flagValOrPanic(long).(int8)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to int8", long))
	}
	return v
}

// Int16 returns the given flag value as int16.
// Panics if not present. Flags must be registered.
func (f FlagMap) Int16(long string) int16 {
	v, ok := f.flagValOrPanic(long).(int16)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to int16", long))
	}
	return v
}

// Int32 returns the given flag value as int32.
// Panics if not present. Flags must be registered.
func (f FlagMap) Int32(long string) int32 {
	v, ok := f.flagValOrPanic(long).(int32)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to int32", long))
	}
	return v
}

// Int64 returns the given flag value as int64.
// Panics if not present. Flags must be registered.
func (f FlagMap) Int64(long string) int64 {
	v, ok := f.flagValOrPanic(long).(int64)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to int64", long))
	}
	return v
}

// Uint returns the given flag value as uint.
// Panics if not present. Flags must be registered.
func (f FlagMap) Uint(long string) uint {
	v, ok := f.flagValOrPanic(long).(uint)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to uint", long))
	}
	return v
}

// Uint8 returns the given flag value as uint8.
// Panics if not present. Flags must be registered.
func (f FlagMap) Uint8(long string) uint8 {
	v, ok := f.flagValOrPanic(long).(uint8)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to uint8", long))
	}
	return v
}

// Uint16 returns the given flag value as uint16.
// Panics if not present. Flags must be registered.
func (f FlagMap) Uint16(long string) uint16 {
	v, ok := f.flagValOrPanic(long).(uint16)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to uint16", long))
	}
	return v
}

// Uint32 returns the given flag value as uint32.
// Panics if not present. Flags must be registered.
func (f FlagMap) Uint32(long string) uint32 {
	v, ok := f.flagValOrPanic(long).(uint32)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to uint32", long))
	}
	return v
}

// Uint64 returns the given flag value as uint64.
// Panics if not present. Flags must be registered.
func (f FlagMap) Uint64(long string) uint64 {
	v, ok := f.flagValOrPanic(long).(uint64)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to uint64", long))
	}
	return v
}

// Float32 returns the given flag value as float32.
// Panics if not present. Flags must be registered.
func (f FlagMap) Float32(long string) float32 {
	v, ok := f.flagValOrPanic(long).(float32)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to float32", long))
	}
	return v
}

// Float64 returns the given flag value as float64.
// Panics if not present. Flags must be registered.
func (f FlagMap) Float64(long string) float64 {
	v, ok := f.flagValOrPanic(long).(float64)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to float64", long))
	}
	return v
}

// Duration returns the given flag value as duration.
// Panics if not present. Flags must be registered.
func (f FlagMap) Duration(long string) time.Duration {
	v, ok := f.flagValOrPanic(long).(time.Duration)
	if !ok {
		panic(fmt.Errorf("failed to assert flag '%s' to duration", long))
	}
	return v
}

// flagValOrPanic is small convenience method that checks if the value for
// the given flag identitifer is set on f.
// If not, a panic is triggered.
func (f FlagMap) flagValOrPanic(long string) interface{} {
	fi, ok := f[long]
	if !ok {
		panic(fmt.Errorf("flag '%s' not registered", long))
	}
	return fi.Value
}
