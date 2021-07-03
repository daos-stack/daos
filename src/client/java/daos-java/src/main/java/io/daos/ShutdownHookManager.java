/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

import java.util.Deque;
import java.util.concurrent.ConcurrentLinkedDeque;

/**
 * A shutdown hook manager to make hooks executed in reverse-added-order. This reversed order
 * is usually suitable for hierarchical resource releasing. Take DAOS client as example, we initialize
 * in below order,
 * 1, DAOS client environment setup
 * 2, DAOS FS client mount
 *
 * <p>
 * When shutdown, resource releasing order is,
 * 1, DAOS FS client unmount
 * 2, DAOS client environment finalize
 */
public final class ShutdownHookManager {

  private static final Deque<Runnable> hookStack = new ConcurrentLinkedDeque<>();

  static {
    Runtime.getRuntime().addShutdownHook(new Thread(() -> {
      Runnable hook;
      while ((hook = hookStack.pollLast()) != null) {
        hook.run();
      }
    }));
  }

  /**
   * add hook.
   *
   * @param runnable
   * runnable task
   */
  public static void addHook(Runnable runnable) {
    hookStack.add(runnable);
  }

  /**
   * remove hook.
   *
   * @param runnable
   * runnable task
   * @return true for successful removing. false otherwise.
   */
  public static boolean removeHook(Runnable runnable) {
    if (runnable == null) {
      return false;
    }
    return hookStack.remove(runnable);
  }
}
