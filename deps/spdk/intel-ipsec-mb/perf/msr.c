/**********************************************************************
  Copyright(c) 2018-2021 Intel Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/

/**
 * @brief Provides access to MSR read & write operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#else
#include <windows.h>
#ifdef WIN_MSR
#include "OlsDef.h"
#include "OlsApiInitExt.h"
#include "OlsApiInit.h"
#endif /* WIN_MSR */
#endif

#include "msr.h"

static int *m_msr_fd = NULL;           /**< MSR driver file descriptors table */
static unsigned m_maxcores = 0;        /**< max number of cores (size of the
                                          table above too) */
#ifdef WIN_MSR
union msr_data {
        uint64_t ui64;
        struct {
                uint32_t low;
                uint32_t high;
        } ui32;
};

HMODULE hOpenLibSys = NULL;

/**
 * @brief Initialize WinRing0 driver
 *
 * @return Operation status
 * @retval MACHINE_RETVAL_OK on success
 */
static int
initMSRdriver(void)
{
        const BOOL result = InitOpenLibSys(&hOpenLibSys);

        if (result == 0) {
                hOpenLibSys = NULL;
                fprintf(stderr, "Failed to load WinRing0 driver!\n");
                return MACHINE_RETVAL_ERROR;
        }

        return MACHINE_RETVAL_OK;
}

/**
 * @brief Shutdown WinRing0 driver
 */
static void
deInitMSRdriver(void)
{
        const BOOL result = DeinitOpenLibSys(&hOpenLibSys);

        if (result == 0)
                fprintf(stderr, "Error shutting down WinRing0 driver!\n");

        hOpenLibSys = NULL;
}
#endif /* WIN_MSR */

int
machine_init(const unsigned max_core_id)
{
        unsigned i;

        if (max_core_id == 0)
                return MACHINE_RETVAL_PARAM;
#ifdef _WIN32
#ifdef WIN_MSR
        if (initMSRdriver() != MACHINE_RETVAL_OK)
                return MACHINE_RETVAL_ERROR;
#else
        fprintf(stderr, "WinRing0 driver not available!\n");
        return MACHINE_RETVAL_ERROR;
#endif /* WIN_MSR */
#endif /* _WIN32 */

        m_maxcores = max_core_id + 1;

        /**
         * Allocate table to hold MSR driver file descriptors
         * Each file descriptor is for a different core.
         * Core id is an index to the table.
         */
        m_msr_fd = (int *)malloc(m_maxcores * sizeof(m_msr_fd[0]));
        if (m_msr_fd == NULL) {
                m_maxcores = 0;
                return MACHINE_RETVAL_ERROR;
        }

        for (i = 0; i < m_maxcores; i++)
                m_msr_fd[i] = -1;

        return MACHINE_RETVAL_OK;
}

int
machine_fini(void)
{
        ASSERT(m_msr_fd != NULL);
        if (m_msr_fd == NULL)
                return MACHINE_RETVAL_ERROR;
#ifdef _WIN32
#ifdef WIN_MSR
        deInitMSRdriver();
#endif
#else
        unsigned i;

        /**
         * Close open file descriptors and free up table memory.
         */
        for (i = 0; i < m_maxcores; i++)
                if (m_msr_fd[i] != -1) {
                        close(m_msr_fd[i]);
                        m_msr_fd[i] = -1;
                }
#endif /* WIN_MSR */
        free(m_msr_fd);
        m_msr_fd = NULL;
        m_maxcores = 0;

        return MACHINE_RETVAL_OK;
}

#ifndef _WIN32
/**
 * @brief Returns MSR driver file descriptor for given core id
 *
 * File descriptor could be previously open and comes from
 * m_msr_fd table or is open (& cached) during the call.
 *
 * @param lcore logical core id
 *
 * @return MSR driver file descriptor corresponding \a lcore
 */
static int
msr_file_open(const unsigned lcore)
{
        ASSERT(lcore < m_maxcores);
        ASSERT(m_msr_fd != NULL);

        int fd = m_msr_fd[lcore];

        if (fd < 0) {
                char fname[32];

                memset(fname, 0, sizeof(fname));
                snprintf(fname, sizeof(fname)-1,
                         "/dev/cpu/%u/msr", lcore);
                fd = open(fname, O_RDWR);
                if (fd < 0)
                        fprintf(stderr, "Error opening file '%s'!\n", fname);
                else
                        m_msr_fd[lcore] = fd;
        }

        return fd;
}
#endif /* _WIN32 */

int
msr_read(const unsigned lcore,
         const uint32_t reg,
         uint64_t *value)
{
        int ret = MACHINE_RETVAL_OK;
#ifdef _WIN32
#ifdef WIN_MSR
        union msr_data msr;
        BOOL status;
#endif
#endif
        ASSERT(value != NULL);
        if (value == NULL)
                return MACHINE_RETVAL_PARAM;

        ASSERT(lcore < m_maxcores);
        if (lcore >= m_maxcores)
                return MACHINE_RETVAL_PARAM;

        ASSERT(m_msr_fd != NULL);
        if (m_msr_fd == NULL)
                return MACHINE_RETVAL_ERROR;
#ifdef _WIN32
#ifdef WIN_MSR
        msr.ui64 = 0;
        status = RdmsrTx((DWORD)reg, &(msr.ui32.low),
                         &(msr.ui32.high), (1ULL << lcore));
        if (status)
                *value = msr.ui64;
        else
                ret = MACHINE_RETVAL_ERROR;
#endif /* WIN_MSR */
#else
        int fd = -1;
        ssize_t read_ret = 0;

        fd = msr_file_open(lcore);
        if (fd < 0)
                return MACHINE_RETVAL_ERROR;

        read_ret = pread(fd, value, sizeof(value[0]), (off_t)reg);

        if (read_ret != sizeof(value[0]))
                ret = MACHINE_RETVAL_ERROR;
#endif /* _WIN32 */
        if (ret != MACHINE_RETVAL_OK)
                fprintf(stderr, "RDMSR failed for reg[0x%x] on lcore %u\n",
                        (unsigned)reg, lcore);

        return ret;
}

int
msr_write(const unsigned lcore,
          const uint32_t reg,
          const uint64_t value)
{
        int ret = MACHINE_RETVAL_OK;
#ifdef _WIN32
#ifdef WIN_MSR
        union msr_data msr;
        BOOL status;
#endif
#endif
        ASSERT(lcore < m_maxcores);
        if (lcore >= m_maxcores)
                return MACHINE_RETVAL_PARAM;

        ASSERT(m_msr_fd != NULL);
        if (m_msr_fd == NULL)
                return MACHINE_RETVAL_ERROR;

#ifdef _WIN32
#ifdef WIN_MSR
        msr.ui64 = value;
        status = WrmsrTx((DWORD)reg, msr.ui32.low,
                         msr.ui32.high, (1ULL << lcore));
        if (!status)
                ret = MACHINE_RETVAL_ERROR;
#endif /* WIN_MSR */
#else
        int fd = -1;
        ssize_t write_ret = 0;

        fd = msr_file_open(lcore);
        if (fd < 0)
                return MACHINE_RETVAL_ERROR;

        write_ret = pwrite(fd, &value, sizeof(value), (off_t)reg);

        if (write_ret != sizeof(value))
                ret = MACHINE_RETVAL_ERROR;
#endif /* _WIN32 */
        if (ret != MACHINE_RETVAL_OK)
                fprintf(stderr, "WRMSR failed for reg[0x%x] "
                        "<- value[0x%llx] on lcore %u\n",
                        (unsigned)reg, (unsigned long long)value, lcore);

        return ret;
}
