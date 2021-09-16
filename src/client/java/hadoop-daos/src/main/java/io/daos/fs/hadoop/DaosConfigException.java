/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

public class DaosConfigException extends RuntimeException {

  public DaosConfigException(String msg) {
    super(msg);
  }

  public DaosConfigException(String msg, Throwable e) {
    super(msg, e);
  }
}
