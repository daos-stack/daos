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
	"sort"
	"strconv"
	"strings"
	"time"
)

type flagItemParser func(value string) (interface{}, error)

type flagItem struct {
	Short    string
	Long     string
	Help     string
	HelpArgs string
	Default  interface{}

	parser          flagItemParser
	allowEmptyValue bool
}

// showDefault returns true, if the default parameter should be shown in a help message.
func (fi *flagItem) showDefault() bool {
	// Only for bool types we do not want to show the default value.
	_, ok := fi.Default.(bool)
	return !ok
}

// Flags holds all the registered flags.
type Flags struct {
	list []*flagItem
}

// empty returns true, if the flags are empty.
func (f *Flags) empty() bool {
	return len(f.list) == 0
}

// sort the flags by their name.
func (f *Flags) sort() {
	sort.Slice(f.list, func(i, j int) bool { return f.list[i].Long < f.list[j].Long })
}

// match returns true, if the given flag matches the given short or long identifier.
func (f *Flags) match(flag, short, long string) bool {
	return (len(short) > 0 && flag == "-"+short) || (len(long) > 0 && flag == "--"+long)
}

// register creates a new flag item in f with the given properties.
// The parser is a func that receives the parsed value from the arguments
// and must convert it to the correctly typed value of its flag.
// To prevent each flag having to check whether the value is empty or not,
// the flag allowEmptyValue can be set to false, if the flag must have a value.
func (f *Flags) register(
	short, long, help, helpArgs string,
	defaultValue interface{},
	allowEmptyValue bool,
	parser flagItemParser,
) {
	// Validate.
	if len(short) > 1 {
		panic(fmt.Errorf("invalid short flag: '%s' - must be a single character", short))
	} else if short == "-" {
		panic(fmt.Errorf("invalid short flag: '%s' - must not equal '-'", short))
	} else if len(long) == 0 {
		panic(fmt.Errorf("empty long flag: short='%s'", short))
	} else if strings.HasPrefix(long, "-") {
		panic(fmt.Errorf("invalid long flag: '%s' - must not start with a '-'", long))
	} else if len(help) == 0 {
		panic(fmt.Errorf("empty flag help message for flag: '%s'", long))
	}

	// Check, that both short and long are unique.
	// Short flags are empty if not set.
	for _, fi := range f.list {
		if fi.Short != "" && short != "" && fi.Short == short {
			panic(fmt.Errorf("flag shortcut '%s' registered twice", short))
		}
		if fi.Long == long {
			panic(fmt.Errorf("flag '%s' registered twice", long))
		}
	}

	// Add the new flag item.
	f.list = append(f.list, &flagItem{
		Short:    short,
		Long:     long,
		Help:     help,
		HelpArgs: helpArgs,
		Default:  defaultValue,

		parser:          parser,
		allowEmptyValue: allowEmptyValue,
	})
}

// parse iterates the given args and parses all found flags from it.
// The leftover, not parsed arguments are returned.
// The parsed flag results are written to res.
func (f *Flags) parse(args []string, res FlagMap) ([]string, error) {
	// There are 3 ways a flag can be given:
	//   1. `--flag`       : identifier only.
	//   2. `--flag value` : identifier and value in separate args.
	//   3. `--flag=value` : identifier and value joined by '=' in same arg.

ParseLoop:
	for len(args) > 0 {
		// Retrieve the next argument.
		a := args[0]

		// If the argument does not start with a hyphen, it is not a flag.
		// We can stop the parsing loop then.
		if !strings.HasPrefix(a, "-") {
			break ParseLoop
		}
		args = args[1:] // Pop the consumed argument.

		// A double dash (--) is used to signify the end of command options,
		// after which only positional arguments are accepted.
		if a == "--" {
			break ParseLoop
		}

		// Check, if we must parse case 3 of the possible flag formats.
		flagValue := ""
		if pos := strings.Index(a, "="); pos > 0 {
			flagValue = a[pos+1:]
			a = a[:pos]
		}

		// Find the registered flag item.
		var (
			parsedVal interface{}
			err       error
		)
		for _, fi := range f.list {
			if f.match(a, fi.Short, fi.Long) {
				// Check, if the flag requires a value and if yes,
				// if there is one left in the arguments.
				// This is case 2 of the possible flag formats.
				if !fi.allowEmptyValue && flagValue == "" {
					if len(args) == 0 {
						return nil, fmt.Errorf("missing value for flag %s", fi.Long)
					}

					flagValue = args[0]
					args = args[1:] // Pop the consumed argument.
				}

				// Run the parser of this flag against the provided value.
				parsedVal, err = fi.parser(flagValue)
				if err != nil {
					return nil, fmt.Errorf("failed to parse flag %s: %v", fi.Long, err)
				}
				res[fi.Long] = &FlagMapItem{Value: parsedVal}

				// Continue to next flag.
				continue ParseLoop
			}
		}

		return nil, fmt.Errorf("invalid flag: %s", a)
	}

	// Set the default value for every flag that has not been
	// provided by the arguments.
	for _, fi := range f.list {
		if _, ok := res[fi.Long]; !ok {
			res[fi.Long] = &FlagMapItem{
				Value:     fi.Default,
				IsDefault: true,
			}
		}
	}

	return args, nil
}

// StringL same as String, but without a shorthand.
func (f *Flags) StringL(long, defaultValue, help string) {
	f.String("", long, defaultValue, help)
}

// String registers a string flag.
func (f *Flags) String(short, long, defaultValue, help string) {
	f.register(short, long, help, "string", defaultValue, false, func(value string) (interface{}, error) {
		return trimQuotes(value), nil
	})
}

// StringListL same as StringList, but without a shorthand.
func (f *Flags) StringListL(long string, defaultValue []string, help string) {
	f.StringList("", long, defaultValue, help)
}

// StringList registers a string list flag.
func (f *Flags) StringList(short, long string, defaultValue []string, help string) {
	// Convert []string default to []interface{} so FlagMap.StringList() can assert it.
	def := make([]interface{}, len(defaultValue))
	for i, v := range defaultValue {
		def[i] = v
	}

	f.register(short, long, help, "stringList", def, false, func(value string) (interface{}, error) {
		return []interface{}{trimQuotes(value)}, nil
	})
}

// BoolL same as Bool, but without a shorthand.
func (f *Flags) BoolL(long string, defaultValue bool, help string) {
	f.Bool("", long, defaultValue, help)
}

// Bool registers a boolean flag.
func (f *Flags) Bool(short, long string, defaultValue bool, help string) {
	f.register(short, long, help, "bool", defaultValue, true, func(value string) (interface{}, error) {
		// For bool flags the value is optional.
		if value == "" {
			return true, nil
		}
		return strconv.ParseBool(value)
	})
}

// IntL same as Int, but without a shorthand.
func (f *Flags) IntL(long string, defaultValue int, help string) {
	f.Int("", long, defaultValue, help)
}

// Int registers an int flag.
func (f *Flags) Int(short, long string, defaultValue int, help string) {
	f.register(short, long, help, "int", defaultValue, false, func(value string) (interface{}, error) {
		return strToInt(value)
	})
}

// Int8L same as Int8, but without a shorthand.
func (f *Flags) Int8L(long string, defaultValue int8, help string) {
	f.Int8("", long, defaultValue, help)
}

// Int8 registers an int8 flag.
func (f *Flags) Int8(short, long string, defaultValue int8, help string) {
	f.register(short, long, help, "int8", defaultValue, false, func(value string) (interface{}, error) {
		return strToInt8(value)
	})
}

// Int16L same as Int16, but without a shorthand.
func (f *Flags) Int16L(long string, defaultValue int16, help string) {
	f.Int16("", long, defaultValue, help)
}

// Int16 registers an int16 flag.
func (f *Flags) Int16(short, long string, defaultValue int16, help string) {
	f.register(short, long, help, "int16", defaultValue, false, func(value string) (interface{}, error) {
		return strToInt16(value)
	})
}

// Int32L same as Int32, but without a shorthand.
func (f *Flags) Int32L(long string, defaultValue int32, help string) {
	f.Int32("", long, defaultValue, help)
}

// Int32 registers an int32 flag.
func (f *Flags) Int32(short, long string, defaultValue int32, help string) {
	f.register(short, long, help, "int32", defaultValue, false, func(value string) (interface{}, error) {
		return strToInt32(value)
	})
}

// Int64L same as Int64, but without a shorthand.
func (f *Flags) Int64L(long string, defaultValue int64, help string) {
	f.Int64("", long, defaultValue, help)
}

// Int64 registers an int64 flag.
func (f *Flags) Int64(short, long string, defaultValue int64, help string) {
	f.register(short, long, help, "int64", defaultValue, false, func(value string) (interface{}, error) {
		return strToInt64(value)
	})
}

// UintL same as Uint, but without a shorthand.
func (f *Flags) UintL(long string, defaultValue uint, help string) {
	f.Uint("", long, defaultValue, help)
}

// Uint registers an uint flag.
func (f *Flags) Uint(short, long string, defaultValue uint, help string) {
	f.register(short, long, help, "uint", defaultValue, false, func(value string) (interface{}, error) {
		return strToUint(value)
	})
}

// Uint8L same as Uint8, but without a shorthand.
func (f *Flags) Uint8L(long string, defaultValue uint8, help string) {
	f.Uint8("", long, defaultValue, help)
}

// Uint8 registers an uint8 flag.
func (f *Flags) Uint8(short, long string, defaultValue uint8, help string) {
	f.register(short, long, help, "uint8", defaultValue, false, func(value string) (interface{}, error) {
		return strToUint8(value)
	})
}

// Uint16L same as Uint16, but without a shorthand.
func (f *Flags) Uint16L(long string, defaultValue uint16, help string) {
	f.Uint16("", long, defaultValue, help)
}

// Uint16 registers an uint16 flag.
func (f *Flags) Uint16(short, long string, defaultValue uint16, help string) {
	f.register(short, long, help, "uint16", defaultValue, false, func(value string) (interface{}, error) {
		return strToUint16(value)
	})
}

// Uint32L same as Uint32, but without a shorthand.
func (f *Flags) Uint32L(long string, defaultValue uint32, help string) {
	f.Uint32("", long, defaultValue, help)
}

// Uint32 registers an uint32 flag.
func (f *Flags) Uint32(short, long string, defaultValue uint32, help string) {
	f.register(short, long, help, "uint32", defaultValue, false, func(value string) (interface{}, error) {
		return strToUint32(value)
	})
}

// Uint64L same as Uint64, but without a shorthand.
func (f *Flags) Uint64L(long string, defaultValue uint64, help string) {
	f.Uint64("", long, defaultValue, help)
}

// Uint64 registers an uint64 flag.
func (f *Flags) Uint64(short, long string, defaultValue uint64, help string) {
	f.register(short, long, help, "uint64", defaultValue, false, func(value string) (interface{}, error) {
		return strToUint64(value)
	})
}

// Float32L same as Float32, but without a shorthand.
func (f *Flags) Float32L(long string, defaultValue float32, help string) {
	f.Float32("", long, defaultValue, help)
}

// Float32 registers an float32 flag.
func (f *Flags) Float32(short, long string, defaultValue float32, help string) {
	f.register(short, long, help, "float32", defaultValue, false, func(value string) (interface{}, error) {
		v, err := strconv.ParseFloat(value, 32)
		return float32(v), err
	})
}

// Float64L same as Float64, but without a shorthand.
func (f *Flags) Float64L(long string, defaultValue float64, help string) {
	f.Float64("", long, defaultValue, help)
}

// Float64 registers an float64 flag.
func (f *Flags) Float64(short, long string, defaultValue float64, help string) {
	f.register(short, long, help, "float64", defaultValue, false, func(value string) (interface{}, error) {
		return strconv.ParseFloat(value, 64)
	})
}

// DurationL same as Duration, but without a shorthand.
func (f *Flags) DurationL(long string, defaultValue time.Duration, help string) {
	f.Duration("", long, defaultValue, help)
}

// Duration registers a duration flag.
func (f *Flags) Duration(short, long string, defaultValue time.Duration, help string) {
	f.register(short, long, help, "duration", defaultValue, false, func(value string) (interface{}, error) {
		return time.ParseDuration(value)
	})
}

// trimQuotes removes a single '"' rune from the start and end of s.
// s is returned unchanged, if it does not have both a prefix and suffix of '"'.
func trimQuotes(s string) string {
	if len(s) >= 2 && s[0] == '"' && s[len(s)-1] == '"' {
		return s[1 : len(s)-1]
	}
	return s
}
