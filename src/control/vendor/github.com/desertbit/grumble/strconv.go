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
	"strconv"
)

// strToInt parses s to an int using strconv.ParseInt with base 0.
func strToInt(s string) (int, error) {
	i, err := strconv.ParseInt(s, 0, 64)
	if err != nil {
		return 0, err
	}

	return int(i), nil
}

// strToInt64 parses s to an int64 using strconv.ParseInt with base 0.
func strToInt64(s string) (int64, error) {
	return strconv.ParseInt(s, 0, 64)
}

// strToInt32 parses s to an int using strconv.ParseInt with base 0.
func strToInt32(s string) (int32, error) {
	i, err := strconv.ParseInt(s, 0, 32)
	if err != nil {
		return 0, err
	}

	return int32(i), nil
}

// strToInt16 parses s to an int using strconv.ParseInt with base 0.
func strToInt16(s string) (int16, error) {
	i, err := strconv.ParseInt(s, 0, 16)
	if err != nil {
		return 0, err
	}

	return int16(i), nil
}

// strToInt8 parses s to an int using strconv.ParseInt with base 0.
func strToInt8(s string) (int8, error) {
	i, err := strconv.ParseInt(s, 0, 8)
	if err != nil {
		return 0, err
	}

	return int8(i), nil
}

// strToUint parses s to an uint using strconv.ParseUint with base 0.
func strToUint(s string) (uint, error) {
	u, err := strconv.ParseUint(s, 0, 64)
	if err != nil {
		return 0, err
	}

	return uint(u), nil
}

// strToUint64 parses s to an uint64 using strconv.ParseUint with base 0.
func strToUint64(s string) (uint64, error) {
	return strconv.ParseUint(s, 0, 64)
}

// strToUint32 parses s to an uint32 using strconv.ParseUint with base 0.
func strToUint32(s string) (uint32, error) {
	u, err := strconv.ParseUint(s, 0, 32)
	if err != nil {
		return 0, err
	}

	return uint32(u), nil
}

// strToUint16 parses s to an uint16 using strconv.ParseUint with base 0.
func strToUint16(s string) (uint16, error) {
	u, err := strconv.ParseUint(s, 0, 16)
	if err != nil {
		return 0, err
	}

	return uint16(u), nil
}

// strToUint8 parses s to an uint8 using strconv.ParseUint with base 0.
func strToUint8(s string) (uint8, error) {
	u, err := strconv.ParseUint(s, 0, 8)
	if err != nil {
		return 0, err
	}

	return uint8(u), nil
}
