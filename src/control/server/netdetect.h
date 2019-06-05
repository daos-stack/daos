//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

#ifndef _NETDETECT_H
#define _NETDETECT_H

extern const int NETDETECT_SUCCESS;
extern const int NETDETECT_FAILURE;
extern const int NETDETECT_ERROR_DLOPEN;
extern const int NETDETECT_ERROR_DLSYM;
extern const int NETDETECT_ERROR_FUNCTION_MISSING;

int netdetectInitialize(char *);
int netdetectCleanup(void);
char * netdetectGetAffinityForIONodes(void);
#endif

