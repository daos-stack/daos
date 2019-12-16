/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.intel.daos.hadoop.fs;

import com.intel.daos.client.DaosObjectType;

/**
 * ALL configuration constants for DAOS filesystem.
 */
public final class Constants {

  public static final String FS_DAOS = "daos";

  // daos pool
  public static final String DAOS_POOL_UUID = "fs.daos.pool.uuid";

  // daos svc
  public static final String DAOS_POOL_SVC = "fs.daos.pool.svc";

  // daos container
  public static final String DAOS_CONTAINER_UUID = "fs.daos.container.uuid";

  // daos chunk size
  public static final String DAOS_CHUNK_SIZE = "fs.daos.chunk.size";
  public static final int DEFAULT_DAOS_CHUNK_SIZE = 1024*1024;


  public static final int DAOS_MODLE = 0755;

  // the maximun of the read buffer size
  public static final String DAOS_READ_BUFFER_SIZE = "fs.daos.read.buffer.size";
  public static final int DEFAULE_DAOS_READ_BUFFER_SIZE = 1 * 1024 * 1024;

  // the maximun of the write buffer size
  public static final String DAOS_WRITE_BUFFER_SIZE = "fs.daos.write.buffer.size";
  public static final int DEFAULT_DAOS_WRITE_BUFFER_SIZE = 1 * 1024 * 1024;

  // split file block size
  public static final String DAOS_BLOCK_SIZE = "fs.daos.block.size";
  public static final int DEFAULT_DAOS_BLOCK_SIZE = 128 * 1024 * 1024;



}
