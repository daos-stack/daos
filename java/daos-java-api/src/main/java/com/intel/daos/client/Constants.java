package com.intel.daos.client;

public final class Constants {

  private Constants() {}

  public static final int SET_XATTRIBUTE_NO_CHECK = 0;
  public static final int SET_XATTRIBUTE_CREATE = 1;
  public static final int SET_XATTRIBUTE_REPLACE = 2;


  public static final String ERROR_NAME_PREFIX = "CUSTOM_ERR";

  public static final int CUSTOM_ERROR_BASE = -1000000;

  public static final ErrorCode CUSTOM_ERR1 = new ErrorCode(-1000001, "scm size and nvme size no greater than 0");
  public static final ErrorCode CUSTOM_ERR2 = new ErrorCode(-1000002, "failed to parse service replics string");
  public static final ErrorCode CUSTOM_ERR3 = new ErrorCode(-1000003, "malloc or realloc buffer failed");
  public static final ErrorCode CUSTOM_ERR4 = new ErrorCode(-1000004, "value length greater than expected");

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
