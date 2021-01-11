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

package io.daos;

import java.nio.ByteOrder;

/**
 * value constants.
 */
public final class Constants {

  private Constants() {}

  public static final ByteOrder DEFAULT_ORDER = ByteOrder.nativeOrder();

  public static final String KEY_CHARSET = "UTF-8";

  public static final int KEY_LIST_BATCH_SIZE_DEFAULT = 128;

  public static final int ENCODED_LENGTH_KEY = 2;

  public static final int ENCODED_LENGTH_EXTENT = 4;

  public static final int KEY_LIST_LEN_DEFAULT = 64;

  public static final byte KEY_LIST_CODE_NOT_STARTED = (byte)0;
  public static final byte KEY_LIST_CODE_IN_USE = (byte)1;
  public static final byte KEY_LIST_CODE_ANCHOR_END = (byte)2;
  public static final byte KEY_LIST_CODE_KEY2BIG = (byte)3;
  public static final byte KEY_LIST_CODE_REACH_LIMIT = (byte)4;

  public static final int KEY_LIST_MAX_BUF_PER_CALL = 64 * 1024;

  public static final String POOL_DEFAULT_SERVER_GROUP = "daos_server";
  public static final String POOL_DEFAULT_RANKS = "0";

  public static final String DUNS_XATTR_NAME = "user.daos";
  public static final String DUNS_XATTR_FMT = "DAOS.%s://%36s/%36s";

  // DAOS will decide what default is. 1MB for now.
  public static final int FILE_DEFAULT_CHUNK_SIZE = 0;
  public static final int FILE_DEFAULT_FILE_MODE = 0755;

  // flags for setting file external attribute, see dfs_setxattr().
  public static final int SET_XATTRIBUTE_NO_CHECK = 0;
  public static final int SET_XATTRIBUTE_CREATE = 1;
  public static final int SET_XATTRIBUTE_REPLACE = 2;

  public static final int ERROR_CODE_NOT_EXIST = 2;
  public static final int ERROR_CODE_FILE_EXIST = 17;
  public static final int ERROR_CODE_ILLEGAL_ARG = -1003;
  public static final int ERROR_CODE_REC2BIG = -2013;

  public static final String ERROR_NAME_PREFIX = "CUSTOM_ERR";

  public static final int CUSTOM_ERROR_BASE = -1000000;

  public static final ErrorCode CUSTOM_ERR_NO_POOL_SIZE =
      new ErrorCode(-1000001, "scm size and nvme size no greater than 0");
  public static final ErrorCode CUSTOM_ERR_INCORRECT_SVC_REPLICS =
      new ErrorCode(-1000002, "failed to parse service replics string");
  public static final ErrorCode CUSTOM_ERR_BUF_ALLOC_FAILED =
      new ErrorCode(-1000003, "malloc or realloc buffer failed");
  public static final ErrorCode CUSTOM_ERR_TOO_LONG_VALUE =
      new ErrorCode(-1000004, "value length greater than expected");
  public static final ErrorCode CUSTOM_ERR_UNS_INVALID =
      new ErrorCode(-1000005, "invalid argument in UNS");
  public static final ErrorCode CUSTOM_ERR_OBJECT_INVALID_ARG =
      new ErrorCode(-1000006, "invalid argument in object");

  public static final int FILE_NAME_LEN_MAX = 255;
  public static final int FILE_PATH_LEN_MAX = 4096;

  public static final int ACCESS_FLAG_FILE_READONLY = 01;
  public static final int ACCESS_FLAG_FILE_READWRITE = 02;
  public static final int ACCESS_FLAG_FILE_CREATE = 0100;
  public static final int ACCESS_FLAG_FILE_EXCL = 0200;

  public static final int MODE_POOL_OTHER_READONLY = 0001;
  public static final int MODE_POOL_OTHER_READWRITE = 0002;
  public static final int MODE_POOL_OTHER_EXECUTE = 0004;

  public static final int MODE_POOL_GROUP_READONLY = 0010;
  public static final int MODE_POOL_GROUP_READWRITE = 0020;
  public static final int MODE_POOL_GROUP_EXECUTE = 0040;

  public static final int MODE_POOL_USER_READONLY = 0100;
  public static final int MODE_POOL_USER_READWRITE = 0200;
  public static final int MODE_POOL_USER_EXECUTE = 0400;

  public static final int ACCESS_FLAG_POOL_READONLY = 1;
  public static final int ACCESS_FLAG_POOL_READWRITE = 2;
  public static final int ACCESS_FLAG_POOL_EXECUTE = 4;

  public static final int ACCESS_FLAG_CONTAINER_READONLY = 1;
  public static final int ACCESS_FLAG_CONTAINER_READWRITE = 2;
  public static final int ACCESS_FLAG_CONTAINER_NOSLIP = 4;

  public static final int UNS_ATTR_NAME_MAX_LEN = 255;
  public static final int UNS_ATTR_VALUE_MAX_LEN = 64 * 1024;
  public static final int UNS_ATTR_VALUE_MAX_LEN_DEFAULT = 1024;
}
