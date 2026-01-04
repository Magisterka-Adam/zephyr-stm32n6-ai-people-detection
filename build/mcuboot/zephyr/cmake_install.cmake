# Install script for directory: /home/a.paleczny/stm_workspace/zephyr

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/home/a.paleczny/zephyr-sdk-0.17.4/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/zephyr/arch/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/zephyr/lib/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/zephyr/soc/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/zephyr/boards/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/zephyr/subsys/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/zephyr/drivers/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/modules/zephyr-stm32-mw-isp/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/modules/cmsis-dsp/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/modules/cmsis_6/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/modules/hal_stm32/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/modules/lvgl/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/modules/mbedtls/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/modules/mcuboot/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/zephyr/kernel/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/zephyr/cmake/flash/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/zephyr/cmake/usage/cmake_install.cmake")
  include("/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/mcuboot/zephyr/cmake/reports/cmake_install.cmake")

endif()

