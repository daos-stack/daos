package pciutils_test

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/pciutils"
)

func TestProvider(t *testing.T) {
	ctx, err := pciutils.Init(test.Context(t))
	if err != nil {
		t.Fatal(err)
	}
	defer pciutils.Fini(ctx)

	cfgBytes := []byte(`00: 86 80 53 09 06 04 10 00 01 02 08 01 00 00 00 00
10: 04 00 00 bc 00 00 00 00 00 00 00 00 00 00 00 00
20: 00 00 00 00 00 00 00 00 00 00 00 00 90 15 a8 00
30: 00 00 00 00 40 00 00 00 00 00 00 00 00 01 00 00
40: 01 50 03 00 08 00 00 00 00 00 00 00 00 00 00 00
50: 11 60 1f 00 00 20 00 00 00 30 00 00 00 00 00 00
60: 10 00 02 00 a1 85 00 10 10 29 09 00 43 6c 41 00
70: 00 00 43 00 00 00 00 00 00 00 00 00 00 00 00 00
80: 00 00 00 00 1f 00 00 00 00 00 00 00 0e 00 00 00
90: 03 00 1f 00 00 00 00 00 00 00 00 00 00 00 00 00
a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
100: 01 00 01 15 00 00 00 00 00 00 00 00 30 20 06 00`)

	dev, err := pciutils.PCIeCapsFromConfig(ctx, cfgBytes)
	if err != nil {
		t.Fatal(err)
	}

	expDev := &hardware.PCIDevice{
		LinkMaxSpeed: 8e+9,
		LinkMaxWidth: 4,
		LinkNegSpeed: 8e+9,
		LinkNegWidth: 4,
	}

	if diff := cmp.Diff(expDev, dev); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}
