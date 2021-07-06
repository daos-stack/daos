/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

import java.io.Closeable;
import java.io.IOException;

/**
 * A interface extended from {@link Closeable} to add one more method {@link #forceClose()}.
 * When it's called, resource cleanup or disconnection should be done immediately no matter
 * what circumstance it is. It's usually called at JVM shutdown.
 */
public interface ForceCloseable extends Closeable {

  /**
   * close at any circumstance.
   *
   * @throws IOException
   */
  void forceClose() throws IOException;
}
