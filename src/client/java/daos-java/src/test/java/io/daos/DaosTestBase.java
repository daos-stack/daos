package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "0a3f7e93-fc33-4e7e-8b56-315b083950c3";
  public static final String DEFAULT_CONT_ID = "4c8daec2-8cc5-4acb-9f0e-8f4af103a32e";

  public static final String DEFAULT_POOL_LABEL = "pool1";
  public static final String DEFAULT_CONT_LABEL = "cont1";

  public static final String DEFAULT_OBJECT_CONT_ID = "44746122-391c-4506-a019-60672d42e64b";

  public static String getPoolId() {
    return System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
  }

  public static String getContId() {
    return System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  public static String getPoolLabel() {
    return System.getProperty("pool_label", DaosTestBase.DEFAULT_POOL_LABEL);
  }

  public static String getContLabel() {
    return System.getProperty("cont_label", DaosTestBase.DEFAULT_CONT_LABEL);
  }

  public static String getObjectContId() {
    return System.getProperty("object_cont_id", DaosTestBase.DEFAULT_OBJECT_CONT_ID);
  }
}
