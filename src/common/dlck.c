#define D_LOGFAC DD_FAC(dlck)

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos_srv/dlck.h>

struct DLCK_callbacks *DLCK_Callbacks = NULL;
