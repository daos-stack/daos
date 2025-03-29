
#define HASH_THREAD
/* sha512 related params and structures*/
#define DIGEST_NWORDS   (SHA512_DIGEST_NWORDS * 2)
#define MB_BUFS         SHA512_MAX_LANES
#define HASH_CTX_MGR    SHA512_HASH_CTX_MGR
#define HASH_CTX	SHA512_HASH_CTX

#define OSSL_THREAD_FUNC	sha512_ossl_func
#define OSSL_HASH_FUNC		SHA512
#define MB_THREAD_FUNC		sha512_mb_func
#define CTX_MGR_INIT		sha512_ctx_mgr_init
#define CTX_MGR_SUBMIT		sha512_ctx_mgr_submit
#define CTX_MGR_FLUSH		sha512_ctx_mgr_flush

#define rounds_buf SHA512_MAX_LANES

#include "md5_thread.c"

#undef HASH_THREAD
