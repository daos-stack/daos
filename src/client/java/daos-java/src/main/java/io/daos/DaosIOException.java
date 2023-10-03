/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

import java.io.IOException;
import java.lang.reflect.Field;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Exception class for remote DAOS operations. The <code>errorCode</code> is passed and defined by DAOS system.
 * The corresponding error message of <code>errorCode</code> is get when {@link #toString()} method is called.
 */
public class DaosIOException extends IOException {

  private int errorCode = Integer.MIN_VALUE;

  private String daosMsg;

  private String parsedMsg;

  protected static final Map<Integer, String> errorMap;

  private static final Logger log = LoggerFactory.getLogger(DaosIOException.class);

  static {
    errorMap = Collections.unmodifiableMap(loadErrorCode());
  }

  private static Map<Integer, String> loadErrorCode() {
    Map<Integer, String> map = new HashMap<>();
    Field[] fields = Constants.class.getDeclaredFields();
    try {
      for (Field f : fields) {
        if (f.getName().startsWith(Constants.ERROR_NAME_PREFIX)) {
          Object o = f.get(null);
          if (o instanceof ErrorCode) {
            ErrorCode ec = (ErrorCode) o;
            map.put(ec.getCode(), ec.getMsg());
          }
        }
      }
    } catch (Exception e) {
      log.error("failed to load error code", e);
      return null;
    }
    return map;
  }

  public DaosIOException(String msg) {
    super(msg);
  }

  public DaosIOException(Throwable cause) {
    super(cause);
  }

  /**
   * Constructor with msg, errorCode and daosMsg.
   *
   * @param msg
   * error message
   * @param errorCode
   * error code
   * @param daosMsg
   * message from native code
   */
  public DaosIOException(String msg, int errorCode, String daosMsg) {
    super(msg);
    this.errorCode = errorCode;
    this.daosMsg = daosMsg;
  }

  /**
   * Constructor with msg, errorCode and cause.
   *
   * @param msg
   * error message
   * @param errorCode
   * error code
   * @param cause
   * cause
   */
  public DaosIOException(String msg, int errorCode, Throwable cause) {
    super(msg, cause);
    this.errorCode = errorCode;
  }

  /**
   * get error code.
   *
   * @return error code
   */
  public int getErrorCode() {
    return errorCode;
  }

  /**
   * get message.
   *
   * @return message string
   */
  @Override
  public String getMessage() {
    return toString();
  }

  /**
   * get localized message.
   *
   * @return localized string
   */
  @Override
  public String getLocalizedMessage() {
    return toString();
  }

  /**
   * exception in string.
   *
   * @return string
   */
  @Override
  public String toString() {
    if (parsedMsg != null) {
      return parsedMsg;
    }
    boolean needSuperMsg = false;
    StringBuilder sb = new StringBuilder(super.getMessage());
    sb.append(" error code: ");
    if (errorCode == Integer.MIN_VALUE) {
      sb.append("unknown.");
      needSuperMsg = true;
    } else {
      sb.append(errorCode);
      if (errorCode < Constants.CUSTOM_ERROR_BASE) {
        needSuperMsg = true;
        daosMsg = errorMap.get(errorCode);
      }
    }
    sb.append(" error msg: ").append(daosMsg == null ? "" : daosMsg);
    if (needSuperMsg && super.getMessage() != null) {
      sb.append(". more msg: ").append(super.getMessage());
    }
    parsedMsg = sb.toString();
    return parsedMsg;
  }
}
