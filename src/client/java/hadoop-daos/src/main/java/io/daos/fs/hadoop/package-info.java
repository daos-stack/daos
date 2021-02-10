/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * DAOS Implementation of Hadoop File System.
 *
 * <pre>
 * To get instance of DAOS Implementation, {@link io.daos.fs.hadoop.DaosFileSystem}, user just needs to make
 * below statements after proper hadoop configuration.
 * <code>
 * Configuration cfg = new Configuration();
 * cfg.set(Constants.DAOS_POOL_UUID, poolUuid);
 * cfg.set(Constants.DAOS_CONTAINER_UUID, containerUuid);
 * cfg.set(Constants.DAOS_POOL_SVC, svc);
 * FileSystem fileSystem = FileSystem.get(URI.create("daos://ip:port/"), cfg);
 * </code>
 * </pre>
 *
 * <p>
 * Be noted the schema is {@link io.daos.fs.hadoop.Constants#DAOS_SCHEMA}
 *
 * <p>
 * For hadoop configuration, please refer {@linkplain io.daos.fs.hadoop.DaosFileSystem DaosFileSystem}
 *
 * @see io.daos.fs.hadoop.DaosFileSystem
 */
package io.daos.fs.hadoop;

