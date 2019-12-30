/**
 * Java interface of <a href=https://github.com/daos-stack/daos>
 * DAOS(Distributed Asynchronous Object Storage)</a> File System.
 *
 * Typical usage:
 * 1, Instantiate {@link com.intel.daos.client.DaosFsClient.DaosFsClientBuilder} as builder
 * 2, Set poolId, containerId and other parameters on builder
 * 3, Call {@link com.intel.daos.client.DaosFsClient.DaosFsClientBuilder#build()} to get {@link com.intel.daos.client.DaosFsClient} instance
 * 4, Call {@linkplain com.intel.daos.client.DaosFsClient#getFile getFile} methods to instantiate {@link com.intel.daos.client.DaosFile}
 * 5, Operate on {@link com.intel.daos.client.DaosFile} instance.
 *
 * After the step 3, you can call below convenient methods directly on {@link com.intel.daos.client.DaosFsClient}
 * <li>{@link com.intel.daos.client.DaosFsClient#mkdir(String, int, boolean)}</li>
 * <li>{@link com.intel.daos.client.DaosFsClient#move(String, String)}</li>
 * <li>{@link com.intel.daos.client.DaosFsClient#delete(String)}</li>
 */


package com.intel.daos.client;