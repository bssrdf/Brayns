# Copyright (c) 2016, EPFL/Blue Brain Project
# All rights reserved. Do not distribute without permission.
# Responsible Author: Daniel.Nachbaur@epfl.ch
#
# This file is part of Brayns <https://github.com/BlueBrain/Brayns>

set(TEST_LIBRARIES ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} brayns)

configure_file(paths.h.in ${PROJECT_BINARY_DIR}/tests/paths.h)
if(TARGET BBPTestData AND TARGET Lunchbox)
  list(APPEND TEST_LIBRARIES BBPTestData Lunchbox)
else()
  list(APPEND EXCLUDE_FROM_TESTS braynsTestData.cpp)
endif()
if(NOT BRAYNS_OSPRAY_ENABLED)
  list(APPEND EXCLUDE_FROM_TESTS brayns.cpp braynsTestData.cpp)
endif()
include(CommonCTest)
