# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/app"
  "/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/app"
  "/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/_sysbuild/sysbuild/images/app-prefix"
  "/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/_sysbuild/sysbuild/images/app-prefix/tmp"
  "/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp"
  "/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/_sysbuild/sysbuild/images/app-prefix/src"
  "/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/a.paleczny/stm_workspace/zephyr-stm32n6-ai-people-detection/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp${cfgdir}") # cfgdir has leading slash
endif()
