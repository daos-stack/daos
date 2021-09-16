/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

/**
 * ALL configuration and value constants.
 */
public final class Constants {

  public static final String DAOS_SCHEMA = "daos";

  public static final String DAOS_CONFIG_CHOICE = "fs.daos.choice";

  public static final String DAOS_DEFAULT_FS = "fs.defaultFS";

  public static final String DAOS_CONFIG_PREFIX = "fs.daos.";

  public static final String DAOS_SERVER_GROUP = "fs.daos.server.group";

  public static final String DAOS_POOL_FLAGS = "fs.daos.pool.flags";
  // daos pool
  public static final String DAOS_POOL_ID = "fs.daos.pool.id";

  // daos container
  public static final String DAOS_CONTAINER_ID = "fs.daos.container.id";

  public static final String DAOS_IO_ASYNC = "fs.daos.io.async";
  public static final boolean DEFAULT_DAOS_IO_ASYNC = true;

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

  public static final String DAOS_WITH_UNS_PREFIX = "fs.daos.with-uns-prefix";
  public static final boolean DEFAULT_DAOS_WITH_UNS_PREFIX = true;

}
