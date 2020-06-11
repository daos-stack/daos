/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.intel.daos.hadoop.fs.multiple;

import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.permission.FsPermission;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public class TestMultipleDaosMkdir {
//    static FileSystem daos ;
//    static CreateDaosFS daosFS ;
//
//    @BeforeClass
//    public static void testDaosInit() throws IOException {
//        daosFS=new CreateDaosFS();
//        daosFS.getPool();
//        daosFS.getContainer();
//
//    }
//
//    @AfterClass
//    public static void close(){
//        System.out.println("@AfterClass");
//        daosFS.close();
//    }
//
//    @Test
//    public void testMultipleMkdir() throws InterruptedException {
//        ExecutorService executor = Executors.newFixedThreadPool(5);
//        executor.submit(new Runnable() {
//            @Override
//            public void run() {
//                try {
//                    daos=daosFS.getFs();
//                    Path f = new Path("/attempt_20191113114701_0024_m_000159_4855");
//                    FsPermission permission = new FsPermission("0755");
//                    boolean status = daos.mkdirs(f,permission);
//                    System.out.println("mkdir result = "+status);
//                } catch (IOException e) {
//                    e.printStackTrace();
//                } finally {
//                    try {
//                        daos.close();
//                    } catch (IOException e) {
//                        e.printStackTrace();
//                    }
//                }
//
//            }
//        });
//        executor.submit(new Runnable() {
//            @Override
//            public void run() {
//                try {
//                    daos=daosFS.getFs();
//                    Path f = new Path("/attempt_20191113114701_0024_m_000159_4855");
//                    FsPermission permission = new FsPermission("0755");
//                    boolean status = daos.mkdirs(f,permission);
//                    System.out.println("mkdir result = "+status);
//                } catch (IOException e) {
//                    e.printStackTrace();
//                } finally {
//                    try {
//                        daos.close();
//                    } catch (IOException e) {
//                        e.printStackTrace();
//                    }
//                }
//
//            }
//        });
//        executor.submit(new Runnable() {
//            @Override
//            public void run() {
//                try {
//                    daos=daosFS.getFs();
//                    Path f = new Path("/attempt_20191113114701_0024_m_000159_4855");
//                    FsPermission permission = new FsPermission("0755");
//                    boolean status = daos.mkdirs(f,permission);
//                    System.out.println("mkdir result = "+status);
//                } catch (IOException e) {
//                    e.printStackTrace();
//                } finally {
//                    try {
//                        daos.close();
//                    } catch (IOException e) {
//                        e.printStackTrace();
//                    }
//                }
//
//            }
//        });
//        executor.submit(new Runnable() {
//            @Override
//            public void run() {
//                try {
//                    daos=daosFS.getFs();
//                    Path f = new Path("/attempt_20191113114701_0024_m_000159_4855");
//                    FsPermission permission = new FsPermission("0755");
//                    boolean status = daos.mkdirs(f,permission);
//                    System.out.println("mkdir result = "+status);
//                } catch (IOException e) {
//                    e.printStackTrace();
//                } finally {
//                    try {
//                        daos.close();
//                    } catch (IOException e) {
//                        e.printStackTrace();
//                    }
//                }
//
//            }
//        });
//        executor.submit(new Runnable() {
//            @Override
//            public void run() {
//                try {
//                    daos=daosFS.getFs();
//                    Thread.sleep(1000);
//                } catch (InterruptedException e) {
//                    e.printStackTrace();
//                } finally {
//                    try {
//                        daos.close();
//                    } catch (IOException e) {
//                        e.printStackTrace();
//                    }
//                }
//
//            }
//        });
//        executor.shutdown();
//        executor.awaitTermination(30, TimeUnit.SECONDS);
//    }
}
