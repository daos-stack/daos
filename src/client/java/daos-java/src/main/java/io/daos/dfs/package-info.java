/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
