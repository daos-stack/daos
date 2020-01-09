/*
 * (C) Copyright 2018-2019 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * Java interface of <a href=https://github.com/daos-stack/daos>
 * DAOS(Distributed Asynchronous Object Storage)</a> File System.
 *
 * <p>
 * Typical usage:
 * 1, Instantiate {@link com.intel.daos.client.DaosFsClient.DaosFsClientBuilder} as builder
 * 2, Set poolId, containerId and other parameters on builder
 * 3, Call {@link com.intel.daos.client.DaosFsClient.DaosFsClientBuilder#build()} to get
 * {@link com.intel.daos.client.DaosFsClient} instance
 * 4, Call {@linkplain com.intel.daos.client.DaosFsClient#getFile getFile} methods to instantiate
 * {@link com.intel.daos.client.DaosFile}
 * 5, Operate on {@link com.intel.daos.client.DaosFile} instance.
 *
 * <p>
 * After the step 3, you can call below convenient methods directly on {@link com.intel.daos.client.DaosFsClient}
 * <li>{@link com.intel.daos.client.DaosFsClient#mkdir(String, int, boolean)}</li>
 * <li>{@link com.intel.daos.client.DaosFsClient#move(String, String)}</li>
 * <li>{@link com.intel.daos.client.DaosFsClient#delete(String)}</li>
 */
package com.intel.daos.client;