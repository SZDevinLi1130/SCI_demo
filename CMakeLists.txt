#
# Copyright (c) 2025 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

if(SYSBUILD AND DEFINED SYSBUILD_CACHE AND EXISTS "${SYSBUILD_CACHE}")
  file(STRINGS "${SYSBUILD_CACHE}" sysbuild_zephyr_dir REGEX "^Zephyr_DIR:PATH=")
  if(sysbuild_zephyr_dir)
    string(REPLACE "Zephyr_DIR:PATH=" "" sysbuild_zephyr_dir "${sysbuild_zephyr_dir}")
    set(Zephyr_DIR "${sysbuild_zephyr_dir}" CACHE PATH "Zephyr package path from sysbuild cache" FORCE)
  endif()

  file(STRINGS "${SYSBUILD_CACHE}" sysbuild_zephyr_base REGEX "^ZEPHYR_BASE:PATH=")
  if(sysbuild_zephyr_base)
    string(REPLACE "ZEPHYR_BASE:PATH=" "" sysbuild_zephyr_base "${sysbuild_zephyr_base}")
    set(ZEPHYR_BASE "${sysbuild_zephyr_base}" CACHE PATH "Zephyr base from sysbuild cache" FORCE)
  endif()
endif()

find_package(Zephyr REQUIRED HINTS ${Zephyr_DIR} $ENV{ZEPHYR_BASE} ${ZEPHYR_BASE})
project(shorter_conn_intervals_test)

FILE(GLOB app_sources src/*.c)
# NORDIC SDK APP START
target_sources(app PRIVATE
  ${app_sources}
)
# NORDIC SDK APP END
