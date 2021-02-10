/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop.perf;

import java.util.Random;

public class Test {

  public static void main(String args[]) {
    int i = 10;
    while (i-- > 0) {
      run();
    }
  }

  private static void run() {
    long fileSize = 1073741824;
    Random rd = new Random();
    int round = 0;
    int exceed = 0;
    long count = 0;
    while (count < fileSize) {
      long offset = (long) (fileSize * rd.nextFloat());
      round++;
      if (offset + 4 * 1024 * 1024 > fileSize) {
        exceed++;
      }
//      System.out.println(offset);
      count += 131072;
    }

    System.out.println("============");
    System.out.println(exceed);
    System.out.println(round);
  }
}
