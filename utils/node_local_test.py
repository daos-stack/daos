#!/usr/bin/env python3
"""Node local test (NLT) entry point.

The implementation now lives in the ``nlt`` package alongside this file; this thin
wrapper preserves the historical ``utils/node_local_test.py`` invocation used by CI
and developer scripts.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import sys

# The nlt package lives under src/tests/nlt; add that directory to the path so it imports.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                'src', 'tests'))

# pylint: disable=wrong-import-position
from nlt.cli import main  # noqa: E402

if __name__ == '__main__':
    main()
