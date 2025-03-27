
#define HASH_THREAD
/* sha1 related params and structures*/
#define DIGEST_NWORDS   SHA1_DIGEST_NWORDS
#define MB_BUFS         SHA1_MAX_LANES
#define HASH_CTX_MGR    SHA1_HASH_CTX_MGR
#define HASH_CTX	SHA1_HASH_CTX

#define OSSL_THREAD_FUNC	sha1_ossl_func
#define OSSL_HASH_FUNC		SHA1
#define MB_THREAD_FUNC		sha1_mb_func
#define CTX_MGR_INIT		sha1_ctx_mgr_init
#define CTX_MGR_SUBMIT		sha1_ctx_mgr_submit
#define CTX_MGR_FLUSH		sha1_ctx_mgr_flush

#define rounds_buf SHA1_MAX_LANES

#include "md5_thread.c"

#undef HASH_THREAD
