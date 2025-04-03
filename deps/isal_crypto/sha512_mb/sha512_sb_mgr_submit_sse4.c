/**********************************************************************
  Copyright(c) 2011-2016 Intel Corporation All rights reserved.

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

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "sha512_mb.h"

/*
 * Function: sha512_sb_mgr_submit_sse4
 *
 * Description: Wrapper API for update routine of single buffer sha512,
 *              to comply with multi-buffer API.
 *
 *              This function will pick up message/digest and length
 *              information from  the argument "job", then call into
 *              sha512_sse4(). Argument "state" is passed in, but not
 *              really used here.
 *
 *              Note: message init and padding is done outside. This function
 *              expects a packed buffer.
 *
 * Argument: state - not really used.
 *           job - contained message, digest, message length information, etc.
 *
 * Return: SHA512_JOB pointer.
 *
 **/
SHA512_JOB *sha512_sb_mgr_submit_sse4(SHA512_MB_JOB_MGR * state, SHA512_JOB * job)
{
	assert(job != NULL);

	uint8_t *buff = job->buffer;
	uint64_t *digest = job->result_digest, len = job->len;

	sha512_sse4((const void *)buff, (void *)digest, len);

	return job;
}
