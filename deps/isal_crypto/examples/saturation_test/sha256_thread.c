
#define HASH_THREAD
/* sha256 related params and structures*/
#define DIGEST_NWORDS   SHA256_DIGEST_NWORDS
#define MB_BUFS         SHA256_MAX_LANES
#define HASH_CTX_MGR    SHA256_HASH_CTX_MGR
#define HASH_CTX	SHA256_HASH_CTX

#define OSSL_THREAD_FUNC	sha256_ossl_func
#define OSSL_HASH_FUNC		SHA256
#define MB_THREAD_FUNC		sha256_mb_func
#define CTX_MGR_INIT		sha256_ctx_mgr_init
#define CTX_MGR_SUBMIT		sha256_ctx_mgr_submit
#define CTX_MGR_FLUSH		sha256_ctx_mgr_flush

#define rounds_buf SHA256_MAX_LANES

#include "md5_thread.c"

#undef HASH_THREAD
