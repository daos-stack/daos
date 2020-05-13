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