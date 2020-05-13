/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * 1, Instantiate {@link io.daos.dfs.DaosFsClient.DaosFsClientBuilder} as builder
 * 2, Set poolId, containerId and other parameters on builder
 * 3, Call {@link io.daos.dfs.DaosFsClient.DaosFsClientBuilder#build()} to get
 * {@link io.daos.dfs.DaosFsClient} instance
 * 4, Call {@linkplain io.daos.dfs.DaosFsClient#getFile getFile} methods to instantiate
 * {@link io.daos.dfs.DaosFile}
 * 5, Operate on {@link io.daos.dfs.DaosFile} instance.
 *
 * <p>
 * After the step 3, you can call below convenient methods directly on {@link io.daos.dfs.DaosFsClient}
 * <li>{@link io.daos.dfs.DaosFsClient#mkdir(String, int, boolean)}</li>
 * <li>{@link io.daos.dfs.DaosFsClient#move(String, String)}</li>
 * <li>{@link io.daos.dfs.DaosFsClient#delete(String)}</li>
 */
package io.daos.dfs;