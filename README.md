# PaCMan

## Overview

PaCMan demo.

## Dependencies

1.- General tools:

* `sudo apt-get install cmake build-essential`
* `sudo apt-get install git`
* `sudo apt-get install cmake-gui`

2.- External libs that can be installed in synaptic/apt-get:

* Boost >= 1.48 (PCL/bham)
* FLANN >= 1.7.1 (PCL)
* Eigen >= 3.0 (PCL)
* VTK >= 5.6 (PCL)
* OpenNI 1.5.4 (PCL)
* OpenCV >= 2.4.6 (bham)
* Expat (bham)
* Freeglut (bham)

Alternatively, dependencies for PCL can be installed following the instructions given at [Install PCL 1.7 from sources](http://pointclouds.org/downloads/source.html).

3.- External lib from private download link:

* [NVIDIA PhysX 2.8](https://www.dropbox.com/sh/2o9e4sgt6xp0e5c/FhYfhRLmvt) (bham) 

4.- ROS system and components can be installed following the instructions from their [website](http://wiki.ros.org/hydro/Installation/Ubuntu), we summarize it here for our settings:

* `sudo sh -c 'echo "deb http://packages.ros.org/ros/ubuntu precise main" > /etc/apt/sources.list.d/ros-latest.list'`
* `wget http://packages.ros.org/ros.key -O - | sudo apt-key add -`
* `sudo apt-get update`
* `sudo apt-get install ros-hydro-desktop-full`
* `sudo apt-get install ros-hydro-moveit-full ros-hydro-octomap ros-hydro-octomap-msgs ros-hydro-openni-launch ros-hydro-openni2-launch` (if you find you need to install additional packages, please complete this list)

5.- PCL library can be installed using our forked repo which is already configured for our settings:

* `git clone https://github.com/pacman-project/pcl.git pcl-trunk-Feb-11-2014`
* `cd pcl-trunk-Feb-11-2014 && mkdir build && cd build`
* `cmake ..`
* `make`
* `sudo make install`

NOTE: In some architectures, PCL and dependant libs such as Grasp and PaCManGrasp, might present compilation problems regarding the lack of low level instructions which are CPU-dependant. If so, edit the file `pcl-trunk-Feb-11-2014/cmake/pcl_find_sse.cmake` at line 18 with your available hardware option `-march=CPU-TYPE` (for Ubuntu 12.04, gcc-4.6.3 options are found here [here](http://gcc.gnu.org/onlinedocs/gcc-4.6.3/gcc/Submodel-Options.html#Submodel-Options) ). This flag is required to compile anything taht depends on PCL.

## PaCMan libraries

### Suggested folder tree

Althought not strictly necessary, the suggested folder tree can be created as:

* `mkdir PACMAN_ROOT`
* `cd PACMAN_ROOT`
* `git clone https://github.com/pacman-project/pacman.git`
* `git clone https://github.com/pacman-project/poseEstimation.git`
* `git clone https://github.com/pacman-project/Golem.git`
* `git clone https://github.com/pacman-project/Grasp.git`

And build the partner libraries following their instructions. It is advised to work locally, instead of installing.

### Build

* cd `/PATH/TO/PACMAN_ROOT/pacman`
* `mkdir build`
* `cd build`
* `cmake-gui ..`

* Set the GRASP_{INCLUDE,BINARIES,LIBRARY}, GOLEM_{INCLUDE,BINARIES,LIBRARY,RESOURCES} and UIBK_POSE_ESTIMATION_EXTERNALLIB paths to point where partner libraries are.

* `make`

NOTE: In case of linker problems (Grasp + PCL) the PCL library should be recompiled using revision: 3cd3608931257c238729f595032b2ffebd9b4698; Author: Jochen Sprickerhof <github@jochen.sprickerhof.de>; Date: 2013-10-28 20:13:08; Message: Merge pull request #340 from juagargi/static_lib_fix; Fix bug to allow static library creation


NOTE: By default, ROS packages are not built, since it is less likely that people has ROS installed. To enable this, check the `BUILD_ROS_PKGS` option and recall the current support is for ROS/hydro. In such case, you need to follow the next subsection instructions before building. 

### Configuration of the [ROS](http://wiki.ros.org/groovy/Installation/Ubuntu#groovy.2BAC8-Installation.2BAC8-DebEnvironment.Environment_setup)[/catkin](http://wiki.ros.org/catkin/Tutorials/create_a_workspace) environment:

Before building the pacman software, ensure you have the ROS environment lodaded. For the current terminal session, type:

* `source /opt/ros/hydro/setup.bash`

To make ROS environment available for all sessions type:

* `echo "source /opt/ros/hydro/setup.bash" >> ~/.bashrc`

After the pacman software is built, you need to load also the created catkin workspace in order to use the pacman ROS packages. For For the current terminal session, type:

* `source /PATH/TO/PACMAN_ROOT/catkin/devel/setup.bash`

Again, To make the catkin environment available for all sessions type:

* `echo "source /PATH/TO/PACMAN_ROOT/catkin/devel/setup.bash" >> ~/.bashrc`

### Changing Golem/Grasp configuration files

All required files should be in the /PATH/TO/PACMAN_ROOT/bin folder. If you need to modify any of these, please, do it here, and type

* `cd /PATH/TO/PACMAN_ROOT/pacman/build`
* `make`


### Configuring poseEstimation module:

Before using poseEstimation node in ROS, OpenNI has to be properly installed from ROS repositories. In order to do so the default installation of PCL 1.7 from repositories has to be completely removed from the system. Then run: 

* 'sudo apt-get install ros-hydro-openni-launch' this will install, appropriate for ROS, OpenNI drivers and the whole PCL 1.7 from ROS-hydro repositories
* ${PCL_INCLUDE_DIRS} in a CMakeFile (pacman/ros_pkgs/perception/pose_estimation_uibk/CMakeLists.txt) has to be set manually to: /usr/include/ni

Additionally, the note from poseEstimation library has to be taken into account:
*You need to set the var `UIBK_POSE_ESTIMATION_EXTERNALLIB` in the (pacman/CMakeLists.txt) to point where the poseEstimation library was built, and check that the build folder is called `build` in there. And you might need to change the paths in `src/pose_estimation_uibk.cpp` for configuration and database location





