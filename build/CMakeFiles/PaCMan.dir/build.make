# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 2.8

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list

# Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# The program to use to edit the cache.
CMAKE_EDIT_COMMAND = /usr/bin/ccmake

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/pacman/CODE/pacman

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/pacman/CODE/pacman/build

# Include any dependencies generated for this target.
include CMakeFiles/PaCMan.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/PaCMan.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/PaCMan.dir/flags.make

# Object files for target PaCMan
PaCMan_OBJECTS =

# External object files for target PaCMan
PaCMan_EXTERNAL_OBJECTS =

/home/pacman/CODE/lib/libPaCMan.a: CMakeFiles/PaCMan.dir/build.make
/home/pacman/CODE/lib/libPaCMan.a: CMakeFiles/PaCMan.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --red --bold "Linking CXX static library /home/pacman/CODE/lib/libPaCMan.a"
	$(CMAKE_COMMAND) -P CMakeFiles/PaCMan.dir/cmake_clean_target.cmake
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/PaCMan.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/PaCMan.dir/build: /home/pacman/CODE/lib/libPaCMan.a
.PHONY : CMakeFiles/PaCMan.dir/build

CMakeFiles/PaCMan.dir/requires:
.PHONY : CMakeFiles/PaCMan.dir/requires

CMakeFiles/PaCMan.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/PaCMan.dir/cmake_clean.cmake
.PHONY : CMakeFiles/PaCMan.dir/clean

CMakeFiles/PaCMan.dir/depend:
	cd /home/pacman/CODE/pacman/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/pacman/CODE/pacman /home/pacman/CODE/pacman /home/pacman/CODE/pacman/build /home/pacman/CODE/pacman/build /home/pacman/CODE/pacman/build/CMakeFiles/PaCMan.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/PaCMan.dir/depend

