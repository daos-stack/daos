package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "5c266867-73e8-4ff2-98cd-54c40e77c767";
  public static final String DEFAULT_CONT_ID = "8797adfe-4cb3-4df7-a4ce-e15be177fe7f";

  public static final String DEFAULT_OBJECT_CONT_ID = "8797adfe-4cb3-4df7-a4ce-e15be177fe7f";

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
