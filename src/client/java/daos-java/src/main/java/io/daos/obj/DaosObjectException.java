/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import java.io.IOException;

/**
 * Exception with DAOS object info.
 */
public class DaosObjectException extends IOException {

  private DaosObjectId oid;

  public DaosObjectException(DaosObjectId oid, String msg, Throwable cause) {
    super(msg, cause);
    this.oid = oid;
  }

  public DaosObjectException(DaosObjectId oid, String msg) {
    super(msg);
    this.oid = oid;
  }

  /**
   * get message.
   * @return
   */
  @Override
  public String getMessage() {
    return toString();
  }

  /**
   * get localized message.
   * @return
   */
  @Override
  public String getLocalizedMessage() {
    return toString();
  }

  @Override
  public String toString() {
    return super.getMessage() + " " + oid;
  }
}
