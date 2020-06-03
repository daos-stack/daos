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
