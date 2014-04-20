#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud_conversion.h>
#include <geometry_msgs/PoseArray.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>

#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/common/transforms.h>
#include <pcl/ros/conversions.h>
#include <pcl_conversions/pcl_conversions.h>

#include <definitions/GraspList.h>
#include <definitions/ObjectList.h>

using namespace std;

namespace visualization
{
 class Visualization
 {
   private:
   	ros::NodeHandle nh_;

    // Node handle in the private namespace
    ros::NodeHandle priv_nh_;
    ros::Subscriber sub_object_poses_;
    ros::Subscriber sub_grasps_;
    ros::Publisher pub_objects_cloud_;
    ros::Publisher pub_gripper_;

    string path_to_object_db_;

   public:

    Visualization(ros::NodeHandle nh) : nh_(nh), priv_nh_("~")
    {
       sub_object_poses_ = nh_.subscribe ("/pose_estimation_uibk/object_poses", 500, &Visualization::callback_pose_estimate, this);
       sub_grasps_ = nh_.subscribe ("/grasp_planner_uibk/grasps", 500, &Visualization::callback_grasps, this);
       pub_objects_cloud_ = nh_.advertise<sensor_msgs::PointCloud2>(nh_.resolveName("/recognized_objects"), 10);
       pub_gripper_ = nh_.advertise<visualization_msgs::MarkerArray>("/gripper_pose", 1 );

       nh_.param<std::string>("path_to_object_database",path_to_object_db_, "");
    }

   	~Visualization() {};

   	void callback_pose_estimate(const definitions::ObjectList &objects);
   	void callback_grasps(const definitions::GraspList &grasps);
   	void poseToMatrix4f(geometry_msgs::Pose pose,Eigen::Matrix4f &mat);
   	pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr read_pcd_object(string obj_name,Eigen::Vector3f color);
   	pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr merge_obj_clouds(vector<pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr > obj_pcds);
   	void visualize_gripper(geometry_msgs::PoseStamped gripper_pose,int &id,visualization_msgs::MarkerArray &markers);
 };

 void Visualization::callback_pose_estimate(const definitions::ObjectList &objects)
 {
   ROS_INFO("pose estimation message received!");

   vector<pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr > obj_pcds;
   Eigen::Vector3f color;
   color(0) = 255; color(1) = 0; color(2) = 0;

   for( size_t i = 0; i < objects.object_list.size(); i++ )
   {
     pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr obj_cloud (new pcl::PointCloud<pcl::PointXYZRGBNormal>);  
     obj_cloud = read_pcd_object(objects.object_list[i].name.data,color);

     Eigen::Matrix4f transform_pose;
     poseToMatrix4f(objects.object_list[i].pose, transform_pose);
     pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr transform_cloud(new pcl::PointCloud<pcl::PointXYZRGBNormal>);
     transformPointCloudWithNormals(*obj_cloud, *transform_cloud, transform_pose); 
     obj_pcds.push_back(transform_cloud);

   }

   pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr merged_cloud = merge_obj_clouds(obj_pcds);
   sensor_msgs::PointCloud2 merged_cloud_ros;
   pcl::toROSMsg(*merged_cloud,merged_cloud_ros);
   merged_cloud_ros.header.frame_id = "world_link";
   pub_objects_cloud_.publish(merged_cloud_ros);
 }

 pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr Visualization::merge_obj_clouds(vector<pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr > obj_pcds)
 {
    pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr merged_cloud (new pcl::PointCloud<pcl::PointXYZRGBNormal>);  
    for( size_t i = 0; i < obj_pcds.size(); i++ )
    {
       for( size_t j = 0; j < obj_pcds[i]->points.size(); j++ )
          merged_cloud->points.push_back(obj_pcds[i]->points[j]);	   
    }

    return merged_cloud;
 } 

 pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr Visualization::read_pcd_object(string obj_name,Eigen::Vector3f color)
 {
   std::string path_to_object(path_to_object_db_);
   obj_name.erase(std::remove(obj_name.begin(),obj_name.end(),'\n'),obj_name.end());
   path_to_object += obj_name;
   path_to_object += ".pcd";

   pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr obj_cloud (new pcl::PointCloud<pcl::PointXYZRGBNormal>);
   if (pcl::io::loadPCDFile<pcl::PointXYZRGBNormal> (path_to_object.c_str(), *obj_cloud) == -1) //* load the file
   {
     ROS_ERROR("Error at reading point cloud  %s... ", path_to_object.c_str());      
     ros::Duration(2).sleep();    
   }
   for( size_t i = 0; i < obj_cloud->points.size(); i++ )
   {
      obj_cloud->points[i].r = color(0);
      obj_cloud->points[i].g = color(1);
      obj_cloud->points[i].b = color(2);	
   }
   return obj_cloud;
 }


 void Visualization::poseToMatrix4f(geometry_msgs::Pose pose,Eigen::Matrix4f &mat)
 {
    mat = Eigen::Matrix4f::Identity(4,4);
    Eigen::Quaternionf q(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);      
    Eigen::Matrix3f rotation ( q.toRotationMatrix() );
    for(size_t i=0;i<3;i++)
    {
        for(size_t j=0;j<3;j++)
        {
            mat(i,j) = rotation(i,j);
        }
    }
    mat(0,3) = pose.position.x;
    mat(1,3) = pose.position.y;
    mat(2,3) = pose.position.z;   
 }

 void Visualization::callback_grasps(const definitions::GraspList &grasps)
 {
    ROS_INFO("grasp planner message received!");
    visualization_msgs::MarkerArray markers;
    int id = 0;
    geometry_msgs::PoseStamped gripper_pose = grasps.grasp_list[0].grasp_trajectory[2].wrist_pose; 
    visualize_gripper(gripper_pose,id,markers);    
    pub_gripper_.publish( markers ); 
 }


void Visualization::visualize_gripper(geometry_msgs::PoseStamped gripper_pose,int &id,visualization_msgs::MarkerArray &markers)
{  
  visualization_msgs::Marker marker;
  marker.header.frame_id = "world_link";
  marker.header.stamp = ros::Time();
  marker.ns = "gripper_pose";
  marker.type = visualization_msgs::Marker::MESH_RESOURCE;
  marker.action = visualization_msgs::Marker::ADD;
  
  marker.scale.x = 1.0;
  marker.scale.y = 1.0;
  marker.scale.z = 1.0;
  marker.id = id;
  marker.pose.position = gripper_pose.pose.position;
  marker.pose.orientation = gripper_pose.pose.orientation;
  marker.color.a = 0.5;
  marker.color.r = 0.8;
  marker.color.g = 1;
  marker.color.b = 0;
  marker.mesh_resource = "package://schunk_description/meshes/sdh/palm.stl";
  markers.markers.push_back(marker);
 
  Eigen::Quaternionf quat(gripper_pose.pose.orientation.w,gripper_pose.pose.orientation.x,gripper_pose.pose.orientation.y,gripper_pose.pose.orientation.z);
  Eigen::Matrix3f rot = quat.toRotationMatrix();
  Eigen::Matrix4f trans_g;
  trans_g.block<3,3>(0,0) = rot;
  trans_g(0,3) = gripper_pose.pose.position.x; trans_g(1,3) = gripper_pose.pose.position.y; trans_g(2,3) = gripper_pose.pose.position.z; 
  trans_g(3,0) = 0; trans_g(3,1) = 0; trans_g(3,2) = 0; trans_g(3,3) = 1;
    
  Eigen::Matrix4f trans_1;
  trans_1 << 
    1,0,0,-0.05,
    0,1,0,-0.02,
    0,0,1,-0.12,
    0,0,0,1;
  
  marker.type = visualization_msgs::Marker::CYLINDER;
  marker.id = ++id; 
  Eigen::Matrix4f trans = trans_g * trans_1.inverse();
  Eigen::Matrix3f rot_mat = trans.block<3,3>(0,0);
  Eigen::Quaternionf quat_(rot_mat);
  
  marker.pose.position.x = trans(0,3);
  marker.pose.position.y = trans(1,3);
  marker.pose.position.z = trans(2,3);
  marker.scale.x = 0.01;
  marker.scale.y = 0.01;
  marker.scale.z = 0.1;
  markers.markers.push_back(marker);
 
  marker.type = visualization_msgs::Marker::CYLINDER;
  marker.id = ++id; 
  Eigen::Matrix4f trans_2;
  trans_2 << 
    1,0,0,-0.03,
    0,1,0,0.04,
    0,0,1,-0.12,
    0,0,0,1;  
  Eigen::Matrix4f trans_2_ = trans_g * trans_2.inverse();
  Eigen::Matrix3f rot_mat_2 = trans_2_.block<3,3>(0,0);
  Eigen::Quaternionf quat_2(rot_mat_2);
  
  marker.pose.position.x = trans_2_(0,3);
  marker.pose.position.y = trans_2_(1,3);
  marker.pose.position.z = trans_2_(2,3);  
  marker.scale.x = 0.01;
  marker.scale.y = 0.01;
  marker.scale.z = 0.1;
  markers.markers.push_back(marker);
  
  marker.type = visualization_msgs::Marker::CYLINDER;
  marker.id = ++id; 
  Eigen::Matrix4f trans_3;
  trans_3 << 
    1,0,0,0.04,
    0,1,0,-0.02,
    0,0,1,-0.12,
    0,0,0,1;   
  Eigen::Matrix4f trans_3_ = trans_g * trans_3.inverse();
  Eigen::Matrix3f rot_mat_3 = trans_3_.block<3,3>(0,0);
  Eigen::Quaternionf quat_3(rot_mat_3);
  
  marker.pose.position.x = trans_3_(0,3);
  marker.pose.position.y = trans_3_(1,3);
  marker.pose.position.z = trans_3_(2,3);  
  marker.scale.x = 0.01;
  marker.scale.y = 0.01;
  marker.scale.z = 0.1;
  markers.markers.push_back(marker); 
}

}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "visualization");
    ros::NodeHandle nh;

    visualization::Visualization node(nh);
    ROS_INFO("Visualization node is ready ...");
    while(ros::ok())
    {
        ros::Rate r(30);
        r.sleep();
        ros::spinOnce();
    }
    return 0;
}