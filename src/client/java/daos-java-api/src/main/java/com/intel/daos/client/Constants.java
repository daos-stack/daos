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

package com.intel.daos.client;

/**
 * value constants.
 */
public final class Constants {

  private Constants() {
  }

  public static final String POOL_DEFAULT_SERVER_GROUP = "daos_server";
  public static final String POOL_DEFAULT_RANKS = "0";
  public static final int POOL_DEFAULT_SVC_REPLICS = 1;

  public static final int FILE_DEFAULT_CHUNK_SIZE = 8192;
  public static final int FILE_DEFAULT_FILE_MODE = 0755;

  public static final int SET_XATTRIBUTE_NO_CHECK = 0;
  public static final int SET_XATTRIBUTE_CREATE = 1;
  public static final int SET_XATTRIBUTE_REPLACE = 2;


  public static final int ERROR_CODE_NOT_EXIST = 2;
  public static final int ERROR_CODE_FILE_EXIST = 17;

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


}
