package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "2224d26b-73d6-4536-9929-b31fd4c255e6";
  public static final String DEFAULT_CONT_ID = "d594705d-9c02-4658-8e81-034c8dcdbdf6";

  public static final String DEFAULT_OBJECT_CONT_ID = "d594705d-9c02-4658-8e81-034c8dcdbdf6";

  public static String getPoolId() {
    return System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
  }

  public static String getContId() {
    return System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  public static String getObjectContId() {
    return System.getProperty("object_cont_id", DaosTestBase.DEFAULT_OBJECT_CONT_ID);
  }
}
