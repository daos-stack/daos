/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

import java.io.IOException;

public class TimedOutException extends IOException {

  public TimedOutException(String msg) {
    super(msg);
  }
}
