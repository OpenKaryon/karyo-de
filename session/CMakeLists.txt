cmake_minimum_required(VERSION 3.21)
project(KaryoSession VERSION 0.1.0 LANGUAGES NONE)
# Session launch/data can be packaged independently of the Qt application graph.
include(GNUInstallDirs)
include(CTest)
include(../cmake/Session.cmake)
