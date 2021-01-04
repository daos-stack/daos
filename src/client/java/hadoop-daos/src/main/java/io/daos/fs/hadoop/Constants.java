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

package io.daos.fs.hadoop;

/**
 * ALL configuration and value constants.
 */
public final class Constants {

  public static final String DAOS_SCHEMA = "daos";

  public static final String UNS_ATTR_NAME_HADOOP = "user.daos.hadoop";

  public static final String DAOS_CONFIG_FILE_NAME = "daos-site.xml";

  public static final String DAOS_DEFAULT_FS = "fs.defaultFS";

  public static final String DAOS_CONFIG_PREFIX = "fs.daos.";

  public static final String DAOS_SERVER_GROUP = "fs.daos.server.group";

  public static final String DAOS_POOL_FLAGS = "fs.daos.pool.flags";
  // daos pool
  public static final String DAOS_POOL_UUID = "fs.daos.pool.uuid";

  // daos svc
  public static final String DAOS_POOL_SVC = "fs.daos.pool.svc";

  // daos container
  public static final String DAOS_CONTAINER_UUID = "fs.daos.container.uuid";

  // the minimum and default daos chunk size, maximum size
  public static final String DAOS_CHUNK_SIZE = "fs.daos.chunk.size";
  public static final int DEFAULT_DAOS_CHUNK_SIZE = 1024 * 1024;
  public static final int MAXIMUM_DAOS_CHUNK_SIZE = Integer.MAX_VALUE;
  public static final int MINIMUM_DAOS_CHUNK_SIZE = 4 * 1024;

  public static final int DAOS_MODLE = 0755;

  // the minimum and default internal read buffer size, maximum size
  public static final String DAOS_READ_BUFFER_SIZE = "fs.daos.read.buffer.size";
  public static final int DEFAULT_DAOS_READ_BUFFER_SIZE = 1 * 1024 * 1024;
  public static final int MAXIMUM_DAOS_READ_BUFFER_SIZE = Integer.MAX_VALUE;
  public static final int MINIMUM_DAOS_READ_BUFFER_SIZE = 64 * 1024;

  public static final String DAOS_READ_MINIMUM_SIZE = "fs.daos.read.min.size";

  // the minimum and default internal write buffer size, maximum size
  public static final String DAOS_WRITE_BUFFER_SIZE = "fs.daos.write.buffer.size";
  public static final int DEFAULT_DAOS_WRITE_BUFFER_SIZE = 1 * 1024 * 1024;
  public static final int MAXIMUM_DAOS_WRITE_BUFFER_SIZE = Integer.MAX_VALUE;
  public static final int MINIMUM_DAOS_WRITE_BUFFER_SIZE = 64 * 1024;

  // default file block size
  public static final String DAOS_BLOCK_SIZE = "fs.daos.block.size";
  public static final int DEFAULT_DAOS_BLOCK_SIZE = 128 * 1024 * 1024;

  // minimum and maximum file block size
  public static final int MINIMUM_DAOS_BLOCK_SIZE = 16 * 1024 * 1024;
  public static final int MAXIMUM_DAOS_BLOCK_SIZE = Integer.MAX_VALUE;

}
