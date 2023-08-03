//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"encoding/json"
	"fmt"
	"sort"
	"strconv"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

func numericMarshaler(v *PoolPropertyValue) ([]byte, error) {
	n, err := v.GetNumber()
	if err != nil {
		return nil, err
	}
	return json.Marshal(n)
}

// PoolProperties returns a map of property names to handlers
// for processing property values.
func PoolProperties() PoolPropertyMap {
	return map[string]*PoolPropHandler{
		"reclaim": {
			Property: PoolProperty{
				Number:      PoolPropertySpaceReclaim,
				Description: "Reclaim strategy",
			},
			values: map[string]uint64{
				"disabled": PoolSpaceReclaimDisabled,
				"lazy":     PoolSpaceReclaimLazy,
				"time":     PoolSpaceReclaimTime,
			},
		},
		"self_heal": {
			Property: PoolProperty{
				Number:      PoolPropertySelfHealing,
				Description: "Self-healing policy",
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					switch n {
					case PoolSelfHealingAutoExclude:
						return "exclude"
					case PoolSelfHealingAutoRebuild:
						return "rebuild"
					case PoolSelfHealingAutoExclude | PoolSelfHealingAutoRebuild:
						return "exclude,rebuild"
					default:
						return "unknown"
					}
				},
			},
			values: map[string]uint64{
				"exclude":         PoolSelfHealingAutoExclude,
				"rebuild":         PoolSelfHealingAutoRebuild,
				"exclude,rebuild": PoolSelfHealingAutoExclude | PoolSelfHealingAutoRebuild,
				"rebuild,exclude": PoolSelfHealingAutoExclude | PoolSelfHealingAutoRebuild,
			},
		},
		"space_rb": {
			Property: PoolProperty{
				Number:      PoolPropertyReservedSpace,
				Description: "Rebuild space ratio",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid space_rb value %s (valid values: 0-100)", s)
					rsPct, err := strconv.ParseUint(strings.ReplaceAll(s, "%", ""), 10, 64)
					if err != nil {
						return nil, rbErr
					}
					if rsPct > 100 {
						return nil, rbErr
					}
					return &PoolPropertyValue{rsPct}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d%%", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"label": {
			Property: PoolProperty{
				Number:      PoolPropertyLabel,
				Description: "Pool label",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					if !LabelIsValid(s) {
						return nil, errors.Errorf("invalid label %q", s)
					}
					return &PoolPropertyValue{s}, nil
				},
			},
		},
		"ec_cell_sz": {
			Property: PoolProperty{
				Number:      PoolPropertyECCellSize,
				Description: "EC cell size",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					b, err := humanize.ParseBytes(s)
					if err != nil || !EcCellSizeIsValid(b) {
						return nil, errors.Errorf("invalid EC Cell size %q", s)
					}

					return &PoolPropertyValue{b}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return humanize.IBytes(n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"rd_fac": {
			Property: PoolProperty{
				Number:      PoolPropertyRedunFac,
				Description: "Pool redundancy factor",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid redun fac value %s (valid values: 0-%d)", s, PoolRedunFacMax)
					rfVal, err := strconv.ParseUint(s, 10, 64)
					if err != nil {
						return nil, rbErr
					}
					if rfVal > PoolRedunFacMax {
						return nil, rbErr
					}
					return &PoolPropertyValue{rfVal}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"ec_pda": {
			Property: PoolProperty{
				Number:      PoolPropertyECPda,
				Description: "Performance domain affinity level of EC",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					ecpdaErr := errors.Errorf("invalid ec_pda value %q", s)
					pdalvl, err := strconv.ParseUint(s, 10, 32)
					if err != nil || !EcPdaIsValid(pdalvl) {
						return nil, ecpdaErr
					}
					return &PoolPropertyValue{pdalvl}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"rp_pda": {
			Property: PoolProperty{
				Number:      PoolPropertyRPPda,
				Description: "Performance domain affinity level of RP",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rppdaErr := errors.Errorf("invalid rp_pda value %q", s)
					pdalvl, err := strconv.ParseUint(s, 10, 32)
					if err != nil || !RpPdaIsValid(pdalvl) {
						return nil, rppdaErr
					}
					return &PoolPropertyValue{pdalvl}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"policy": {
			Property: PoolProperty{
				Number:      PoolPropertyPolicy,
				Description: "Tier placement policy",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					if !PoolPolicyIsValid(s) {
						return nil, errors.Errorf("invalid policy string %q", s)
					}
					return &PoolPropertyValue{s}, nil

				},
			},
		},
		"global_version": {
			Property: PoolProperty{
				Number:      PoolPropertyGlobalVersion,
				Description: "Global Version",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					gvErr := errors.Errorf("invalid global version %q", s)
					gvvl, err := strconv.ParseUint(s, 10, 32)
					if err != nil {
						return nil, gvErr
					}
					return &PoolPropertyValue{gvvl}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"upgrade_status": {
			Property: PoolProperty{
				Number:      PoolPropertyUpgradeStatus,
				Description: "Upgrade Status",
			},
			values: map[string]uint64{
				"not started": PoolUpgradeStatusNotStarted,
				"in progress": PoolUpgradeStatusInProgress,
				"completed":   PoolUpgradeStatusCompleted,
				"failed":      PoolUpgradeStatusFailed,
			},
		},
		"scrub": {
			Property: PoolProperty{
				Number:      PoolPropertyScrubMode,
				Description: "Checksum scrubbing mode",
			},
			values: map[string]uint64{
				"off":   PoolScrubModeOff,
				"lazy":  PoolScrubModeLazy,
				"timed": PoolScrubModeTimed,
			},
		},
		"scrub-freq": {
			Property: PoolProperty{
				Number:      PoolPropertyScrubFreq,
				Description: "Checksum scrubbing frequency",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid Scrubbing Frequency value %s", s)
					rsPct, err := strconv.ParseUint(strings.ReplaceAll(s, "%", ""), 10, 64)
					if err != nil {
						return nil, rbErr
					}
					return &PoolPropertyValue{rsPct}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"scrub-thresh": {
			Property: PoolProperty{
				Number:      PoolPropertyScrubThresh,
				Description: "Checksum scrubbing threshold",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid Scrubbing Threshold value %q", s)
					rsPct, err := strconv.ParseUint(strings.ReplaceAll(s, "%", ""), 10, 64)
					if err != nil {
						return nil, rbErr
					}
					return &PoolPropertyValue{rsPct}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"perf_domain": {
			Property: PoolProperty{
				Number:      PoolPropertyPerfDomain,
				Description: "Pool performance domain",
			},
			values: map[string]uint64{
				"root":  PoolPerfDomainRoot,
				"group": PoolPerfDomainGrp,
			},
		},
		"svc_rf": {
			Property: PoolProperty{
				Number:      PoolPropertySvcRedunFac,
				Description: "Pool service redundancy factor",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					svcRFErr := errors.Errorf("invalid service redundancy factor value %s (valid values: 0-%d)", s, PoolSvcRedunFacMax)
					svcRFVal, err := strconv.ParseUint(s, 10, 64)
					if err != nil {
						return nil, svcRFErr
					}
					if svcRFVal > PoolSvcRedunFacMax {
						return nil, svcRFErr
					}
					return &PoolPropertyValue{svcRFVal}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"svc_list": {
			Property: PoolProperty{
				Number:      PoolPropertySvcList,
				Description: "Pool service replica list",
				valueHandler: func(string) (*PoolPropertyValue, error) {
					return nil, errors.New("cannot set pool service replica list")
				},
				valueStringer: func(v *PoolPropertyValue) string {
					return v.String()
				},
				valueMarshaler: func(v *PoolPropertyValue) ([]byte, error) {
					rs, err := ranklist.CreateRankSet(v.String())
					if err != nil {
						return nil, err
					}
					return json.Marshal(rs.Ranks())
				},
			},
		},
		"checkpoint": {
			Property: PoolProperty{
				Number:      PoolPropertyCheckpointMode,
				Description: "WAL Checkpointing behavior",
			},
			values: map[string]uint64{
				"disabled": PoolCheckpointDisabled,
				"timed":    PoolCheckpointTimed,
				"lazy":     PoolCheckpointLazy,
			},
		},
		"checkpoint_freq": {
			Property: PoolProperty{
				Number:      PoolPropertyCheckpointFreq,
				Description: "WAL Checkpointing frequency, in seconds",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid Checkpointing Frequency value %s", s)
					rsPct, err := strconv.ParseUint(s, 10, 64)
					if err != nil {
						return nil, rbErr
					}
					return &PoolPropertyValue{rsPct}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"checkpoint_thresh": {
			Property: PoolProperty{
				Number:      PoolPropertyCheckpointThresh,
				Description: "Usage of WAL before checkpoint is triggered, as a percentage",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid Checkpointing threshold value %s", s)
					rsPct, err := strconv.ParseUint(s, 10, 32)
					if err != nil {
						return nil, rbErr
					}
					return &PoolPropertyValue{rsPct}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
	}
}

func PoolDeprecatedProperties() map[string]string {
	return map[string]string{
		"rf": "rd_fac",
	}
}

// GetProperty returns a *PoolProperty for the property name, if valid.
func (m PoolPropertyMap) GetProperty(name string) (*PoolProperty, error) {
	h, found := m[name]
	if !found {
		return nil, errors.Errorf("unknown property %q", name)
	}
	return h.GetProperty(name), nil
}

// PoolPropertyValue encapsulates the logic for storing a string or uint64
// property value.
type PoolPropertyValue struct {
	data interface{}
}

// SetString sets the property value to a string.
func (ppv *PoolPropertyValue) SetString(strVal string) {
	ppv.data = strVal
}

// SetNumber sets the property value to a number.
func (ppv *PoolPropertyValue) SetNumber(numVal uint64) {
	ppv.data = numVal
}

func (ppv *PoolPropertyValue) IsSet() bool {
	return ppv != nil && ppv.data != nil
}

func (ppv *PoolPropertyValue) String() string {
	if !ppv.IsSet() {
		return "value not set"
	}

	switch v := ppv.data.(type) {
	case string:
		return v
	case uint64:
		return strconv.FormatUint(v, 10)
	default:
		return fmt.Sprintf("unknown data type for %+v", ppv.data)
	}
}

// GetNumber returns the numeric value set for the property,
// or an error if the value is not a number.
func (ppv *PoolPropertyValue) GetNumber() (uint64, error) {
	if !ppv.IsSet() {
		return 0, errors.New("value not set")
	}
	if v, ok := ppv.data.(uint64); ok {
		return v, nil
	}
	return 0, errors.Errorf("%+v is not uint64", ppv.data)
}

// PoolProperty contains a name/value pair representing a pool property.
type PoolProperty struct {
	Number         uint32            `json:"-"`
	Name           string            `json:"name"`
	Description    string            `json:"description"`
	Value          PoolPropertyValue `json:"value"`
	valueHandler   func(string) (*PoolPropertyValue, error)
	valueStringer  func(*PoolPropertyValue) string
	valueMarshaler func(*PoolPropertyValue) ([]byte, error)
}

func (p *PoolProperty) SetValue(strVal string) error {
	if p.valueHandler == nil {
		p.Value.data = strVal
		return nil
	}
	v, err := p.valueHandler(strVal)
	if err != nil {
		return err
	}
	p.Value = *v
	return nil
}

func (p *PoolProperty) String() string {
	if p == nil {
		return "<nil>"
	}

	return p.Name + ":" + p.StringValue()
}

func (p *PoolProperty) StringValue() string {
	if p == nil {
		return "<nil>"
	}
	if p.valueStringer != nil {
		return p.valueStringer(&p.Value)
	}
	return p.Value.String()
}

func (p *PoolProperty) MarshalJSON() (_ []byte, err error) {
	if p == nil {
		return nil, errors.New("nil property")
	}

	var value json.RawMessage
	if p.valueMarshaler != nil {
		if value, err = p.valueMarshaler(&p.Value); err != nil {
			return nil, err
		}
	} else {
		if value, err = json.Marshal(p.StringValue()); err != nil {
			return nil, err
		}
	}

	type toJSON PoolProperty
	return json.Marshal(&struct {
		*toJSON
		Value json.RawMessage `json:"value"`
	}{
		Value:  value,
		toJSON: (*toJSON)(p),
	})
}

type valueMap map[string]uint64

func (m valueMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	return keys
}

type PoolPropHandler struct {
	Property PoolProperty
	values   valueMap
}

func (pph *PoolPropHandler) Values() []string {
	return pph.values.Keys()
}

func (pph *PoolPropHandler) GetProperty(name string) *PoolProperty {
	if pph.Property.valueHandler == nil {
		pph.Property.valueHandler = func(in string) (*PoolPropertyValue, error) {
			if val, found := pph.values[strings.ToLower(in)]; found {
				return &PoolPropertyValue{val}, nil
			}
			return nil, errors.Errorf("invalid value %q for %s (valid: %s)", in, name, strings.Join(pph.Values(), ","))
		}
	}

	if pph.Property.valueStringer == nil {
		valNameMap := make(map[uint64]string)
		for name, number := range pph.values {
			valNameMap[number] = name
		}

		pph.Property.valueStringer = func(v *PoolPropertyValue) string {
			n, err := v.GetNumber()
			if err == nil {
				if name, found := valNameMap[n]; found {
					return name
				}
			}
			return v.String()
		}
	}

	pph.Property.Name = name
	return &pph.Property
}

type PoolPropertyMap map[string]*PoolPropHandler

func (m PoolPropertyMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	return keys
}
