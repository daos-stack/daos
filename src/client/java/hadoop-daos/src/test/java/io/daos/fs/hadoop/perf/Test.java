/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
