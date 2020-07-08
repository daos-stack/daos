package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "07f519b1-f06a-4411-b0f5-638cc39d3825";
  public static final String DEFAULT_CONT_ID = "f393e87a-3e6d-4434-952a-3704ba12914f";

  public static final String DEFAULT_OBJECT_CONT_ID = "dbee873b-3405-4a28-8cba-3394621464a2";

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
