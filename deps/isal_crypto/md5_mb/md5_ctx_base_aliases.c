/**********************************************************************
  Copyright(c) 2019 Arm Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Arm Corporation nor the names of its
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
#include "md5_mb.h"
extern void md5_ctx_mgr_init_base(MD5_HASH_CTX_MGR * mgr);
extern MD5_HASH_CTX *md5_ctx_mgr_flush_base(MD5_HASH_CTX_MGR * mgr);
extern MD5_HASH_CTX *md5_ctx_mgr_submit_base(MD5_HASH_CTX_MGR * mgr, MD5_HASH_CTX * ctx,
					     const void *buffer, uint32_t len,
					     HASH_CTX_FLAG flags);
void md5_ctx_mgr_init(MD5_HASH_CTX_MGR * mgr)
{
	md5_ctx_mgr_init_base(mgr);
}

MD5_HASH_CTX *md5_ctx_mgr_flush(MD5_HASH_CTX_MGR * mgr)
{
	return md5_ctx_mgr_flush_base(mgr);
}

MD5_HASH_CTX *md5_ctx_mgr_submit(MD5_HASH_CTX_MGR * mgr, MD5_HASH_CTX * ctx,
				 const void *buffer, uint32_t len, HASH_CTX_FLAG flags)
{
	return md5_ctx_mgr_submit_base(mgr, ctx, buffer, len, flags);
}
