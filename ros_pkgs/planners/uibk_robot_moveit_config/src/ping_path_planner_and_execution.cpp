#include <vector>
#include <string>

#include <ros/ros.h>
#include <ros/message_operations.h>
#include <std_msgs/String.h>
#include <geometry_msgs/PoseStamped.h>

//for the messages used in the services
#include "definitions/TrajectoryPlanning.h"
#include "definitions/TrajectoryExecution.h"

int main(int argc, char **argv)
{

    ros::init(argc, argv, "ping_path_planner");
    ros::NodeHandle nh;

    // planning service
    std::string planning_service_name("/trajectory_planning_srv");
    if ( !ros::service::waitForService(planning_service_name, ros::Duration().fromSec(1.0)) )
    { 
      ROS_ERROR("After one second, the service %s hasn't shown up...",  planning_service_name.c_str());
      return (-1);     
    }

    // create a test trajectory
    definitions::Grasp grasp;
    ros::Duration five_seconds(5.0);

    // resize with the number of waypoints you want to test
    grasp.grasp_trajectory.resize(1);

    // there is suppose to be solution for this pose, since it was taken from the moveit-gui
    grasp.grasp_trajectory[0].wrist_pose.pose.position.x = 0.114;
    grasp.grasp_trajectory[0].wrist_pose.pose.position.y = 0.349;
    grasp.grasp_trajectory[0].wrist_pose.pose.position.z = 0.445;
    grasp.grasp_trajectory[0].wrist_pose.pose.orientation.x = 0.368;
    grasp.grasp_trajectory[0].wrist_pose.pose.orientation.y = 0.687;
    grasp.grasp_trajectory[0].wrist_pose.pose.orientation.z = 0.600;
    grasp.grasp_trajectory[0].wrist_pose.pose.orientation.w = -0.182;

    // create the service instance
    definitions::TrajectoryPlanning trajectory_planning_srv;
    trajectory_planning_srv.request.ordered_grasp.push_back(grasp);

    // call the planning service with the instance
    ROS_INFO("Calling the planning service");
    if ( !ros::service::call( planning_service_name, trajectory_planning_srv) )
    { 
        ROS_ERROR("Call to the service %s failed.", planning_service_name.c_str());  
        return (-1);
    }   

    if (trajectory_planning_srv.response.result == trajectory_planning_srv.response.OTHER_ERROR)
    {   
        ROS_ERROR("Unable to plan a trajectory: OTHER_ERROR");
        return (-1);
    }

    if (trajectory_planning_srv.response.result == trajectory_planning_srv.response.NO_FEASIBLE_TRAJECTORY_FOUND)
    {   
        ROS_ERROR("Unable to plan a trajectory:: NO_FEASIBLE_TRAJECTORY_FOUND");
        return (-1);
    }

    if (trajectory_planning_srv.response.result == trajectory_planning_srv.response.SUCCESS)
    { 
        ROS_INFO("Trajectory Planning OK, now test execution after 10 seconds...\n");
    }

    // wait 10 seconds before calling the execution service
    ros::Duration(10).sleep();

    // execution service
    std::string execution_service_name("/trajectory_execution_srv");
    if ( !ros::service::waitForService(execution_service_name, ros::Duration().fromSec(1.0)) )
    { 
      ROS_ERROR("After one second, the service %s hasn't shown up...",  execution_service_name.c_str());
      return (-1);     
    }

    // create the service instance
    definitions::TrajectoryExecution trajectory_execution_srv;
    trajectory_execution_srv.request.trajectory = trajectory_planning_srv.response.trajectory;

    // call the execution service with the instance
    ROS_INFO("Calling the execution service");
    if ( !ros::service::call( execution_service_name, trajectory_execution_srv) )
    { 
        ROS_ERROR("Call to the service %s failed.", execution_service_name.c_str());  
        return (-1);
    }   

    if (trajectory_execution_srv.response.result == trajectory_execution_srv.response.OTHER_ERROR)
    {   
        ROS_ERROR("Unable to execute the trajectory: OTHER_ERROR");
        return (-1);
    }

    if (trajectory_execution_srv.response.result == trajectory_execution_srv.response.SUCCESS)
    { 
        ROS_INFO("Trajectory execution OK... pinging done succesfully!\n");
    }

    return 0;
}