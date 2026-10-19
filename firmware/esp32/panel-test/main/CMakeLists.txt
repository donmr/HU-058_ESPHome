# The source lives in ../src so PlatformIO and ESP-IDF share one copy.
idf_component_register(SRCS "../src/main.cpp"
                       INCLUDE_DIRS "../src")
