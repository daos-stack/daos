/* (C) Copyright 2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B620873.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#include <stdio.h>

#define FOREACH_MACRO(ACTION)	\
	ACTION(foo)		\
	ACTION(bar)		\
	ACTION(foobar)		\
	ACTION(temp)		\
	ACTION(tmp2)		\
	ACTION(tmp3)		\
	ACTION(tmp4)		\
	ACTION(tmp5)

#define DEFINE_ENUM(name) name,

#define DEFINE_STRING(name) #name,

#define DO_PRINT(name) printf("%s\n", __names[name]);


enum {
	FOREACH_MACRO(DEFINE_ENUM)
};

const char *__names[] = {
	FOREACH_MACRO(DEFINE_STRING)
};

int main(int argc, char **argv)
{
	FOREACH_MACRO(DO_PRINT)
	return 0;
}
