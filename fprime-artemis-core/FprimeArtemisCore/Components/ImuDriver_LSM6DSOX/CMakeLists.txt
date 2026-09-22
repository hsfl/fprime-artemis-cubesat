####
# ImuDriver_LSM6DSOX component
####
register_fprime_module(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/ImuDriver_LSM6DSOX.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/ImuDriver_LSM6DSOX.cpp"
)
