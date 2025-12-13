cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(air_ctrl)

# Application sources
target_sources(app PRIVATE
    src/main.c
    src/air_ctrl_bt.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(CONFIG_AIR_CTRL_USE_BSEC)
    set(BSEC_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext/algo/bsec_IAQ_Sel/inc)
    set(BSEC_CFG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext/algo/bsec_IAQ_Sel/config/bme688/bme688_sel_33v_3s_28d)
    set(BSEC_SEL_C ${BSEC_CFG_DIR}/bsec_selectivity.c)
    set(BSEC_LIB ${CMAKE_CURRENT_SOURCE_DIR}/ext/algo/bsec_IAQ_Sel/bin/gcc/Cortex_M4F/libalgobsec.a)

    if(NOT EXISTS ${BSEC_LIB})
        message(FATAL_ERROR "CONFIG_AIR_CTRL_USE_BSEC is enabled but BSEC library was not found at: ${BSEC_LIB}")
    endif()

    target_sources(app PRIVATE
        src/air_ctrl_sensor_bsec.c
        ${BSEC_SEL_C}
    )

    target_include_directories(app PRIVATE
        ${BSEC_INC_DIR}
        ${BSEC_CFG_DIR}
    )

    target_link_libraries(app PRIVATE
        ${BSEC_LIB}
    )
else()
    target_sources(app PRIVATE
        src/air_ctrl_sensor_raw.c
    )
endif()

