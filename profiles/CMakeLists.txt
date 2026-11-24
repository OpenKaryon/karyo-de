cmake_minimum_required(VERSION 3.21)
project(KaryoProfileAccounts VERSION 0.1.2 LANGUAGES NONE)
# The accounts helpers use Python and native account tools, independently of Qt.
include(GNUInstallDirs)
include(CTest)
include(../cmake/Profiles.cmake)
