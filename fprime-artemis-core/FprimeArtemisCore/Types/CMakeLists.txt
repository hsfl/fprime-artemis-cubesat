####
# Types shared across components or deployments.
#
# FcPcLink.fpp: anything that crosses the FC<->PC link, so both
# FlightControllerDeployment and PayloadComputerDeployment are built from the
# same definition.
#
# Imu.fpp: the ImuManager <-> IMU driver contract. It lives here rather than in
# either component so the manager does not depend on a specific driver.
#
# Gps.fpp: the same idea for the GpsManager <-> GPS driver contract.
#
# Thermal.fpp: the same idea for the ThermalManager <-> thermal driver contract.
#
# Payload.fpp: the same idea for the PayloadManager <-> payload driver
# contract. PayloadState also crosses the FC<->PC link.
####
register_fprime_module(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/FcPcLink.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/Imu.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/Gps.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/Thermal.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/Payload.fpp"
)
