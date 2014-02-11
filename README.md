# PaCMan

## Overview

PaCMan demo.

## Dependencies

### General 3rd party software dependencies

* Boost >= 1.46 (PCL)
* FLANN >= 1.7.1 (PCL)
* Eigen >= 3.0 (PCL)
* VTK >= 5.6 (PCL)
* OpenNI 1.5.4 (PCL)
* PCL 1.7
* ROS Hydro (uibk, unipi)
* OpenCV >= 2.4.6 (bham)
* Expat (bham)
* Freeglut (bham)
* NVIDIA PhysX 2.8 (bham)

## Installation

### General prerequisites

* `sudo apt-get install cmake build-essential`

* `sudo apt-get install git`

* `sudo apt-get install cmake-gui`

* Boost installation: `sudo apt-get install libboost-date-time1.46.1 libboost-date-time1.46-dev libboost-filesystem1.46.1 libboost-filesystem1.46-dev libboost-iostreams1.46.1 libboost-iostreams1.46-dev libboost-mpi1.46.1 libboost-mpi1.46-dev libboost-serialization1.46.1 libboost-serialization1.46-dev libboost-system1.46.1 libboost-system1.46-dev libboost-thread1.46.1 libboost-thread1.46-dev`

### PCL

* `sudo sh -c 'echo "deb http://packages.ros.org/ros/ubuntu precise main" > /etc/apt/sources.list.d/ros-latest.list'`

* `wget http://packages.ros.org/ros.key -O - | sudo apt-key add -`

* `sudo apt-get update`

* `sudo apt-get install libflann1 libflann-dev libeigen3-dev libvtk5.8 libvtk5.8-qt4 libvtk5-dev libvtk5-qt4-dev libopenni-dev`

### [Installation PCL 1.7 (from sources)](http://pointclouds.org/downloads/source.html)

* `git clone https://github.com/PointCloudLibrary/pcl pcl-trunk`

* `cd pcl-trunk && mkdir BUILD && cd BUILD`

* `cmake-gui ..`

* Press **Configure** and then choose Unix makefiles target.

* Set **Grouped** to group options.

* In **BUILD** group set options: **BUILD_apps** 

* Press **Configure** and then choose **BUILD_app_3d_rec_framework**.

* Press **Configure** and then **Generate**.

* Build and install PCL: `sudo make install`

* In some architectures, PCL and dependant libs such as Grasp, might present compilation problems regarding the lack of low level instructions. If so, edit PCLROOT/cmake/pcl_find_sse.cmake with your available hardware options `-march=CPU-TYPE (for Ubuntu 12.04, gcc-4.6.3) [here](http://gcc.gnu.org/onlinedocs/gcc-4.6.3/gcc/Submodel-Options.html#Submodel-Options)


### Installing PaCMan soft


#### Step 0. Suggested folder tree

* `mkdir pacman`

* `cd pacman`

* `git clone https://github.com/pacman-project/pacman.git`

* `git clone https://github.com/PARTNERLIBS`

Build PARTNERLIBS.


#### Step 1. Configure your [ROS environment](http://wiki.ros.org/groovy/Installation/Ubuntu#groovy.2BAC8-Installation.2BAC8-DebEnvironment.Environment_setup)

Only if you haven't done it from installing the prerequisites. For the current terminal session, type:

* `source /opt/ros/hydro/setup.bash`

To set permamently the ROS environment for future sessions type:

* `echo "source /opt/ros/hydro/setup.bash" >> ~/.bashrc`

#### Step 3. Typicall cmake building

* `mkdir build`

* `cd build`

* `cmake-gui ..`  (<- set the directories for partner libraries)

* `make -jN` (<- N the number of processor to speed up compilation)


#### Step 3. Configure the pacman [catkin environment](http://wiki.ros.org/catkin/Tutorials/create_a_workspace) for use:

In order to use the ros packages, you need to:

* `source PACMAN_ROOT/catkin/devel/setup.bash`

Again, to set permamently the catkin environment for future terminal sessions type:

* `echo "source PACMAN_ROOT/catkin/devel/setup.bash" >> ~/.bashrc`







