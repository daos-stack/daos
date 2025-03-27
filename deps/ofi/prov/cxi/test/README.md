*SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only 
Copyright (c) 2020-2023 Hewlett Packard Enterprise Development LP*

# Libfabric CXI Provider Tests

All tests in this directory are built under the Criterion tool. See [https://criterion.readthedocs.io/en/master/index.html](url).

Common setup/teardown routines are found in cxip_test_common.c.

Collections of related tests are found in the other files.

The build produces an executable cxitest, which runs the pre-supplied Criterion main() function, and supports selecting launch of individual tests, or the entire test suite.

## Running Tests

See the test.sh file for examples of launching tests with cxitest.
