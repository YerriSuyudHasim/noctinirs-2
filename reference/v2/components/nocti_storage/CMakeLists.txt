# components/nocti_storage/CMakeLists.txt
idf_component_register(
    SRCS 
        "nocti_storage.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        freertos 
        nocti_common 
        nocti_stream 
        nocti_timebase 
        log
)
