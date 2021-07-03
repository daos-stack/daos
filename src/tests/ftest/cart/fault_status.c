/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
int main(void)
{
#if FAULT_INJECTION
    return 0;
#else
    return 1;
#endif
}

