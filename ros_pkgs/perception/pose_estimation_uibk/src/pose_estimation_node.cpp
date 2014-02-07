#include <string>
#include <iostream>
#include <vector>

// ROS headers
#include <ros/ros.h>
#include <ros/message_operations.h>
#include <std_srvs/Empty.h>
#include <geometry_msgs/PoseArray.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud_conversion.h>
#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>
#include <definitions/PoseEstimation.h>
#include <definitions/KinectGrabberService.h>
#include <pcl_ros/point_cloud.h>
#include <pcl_conversions/pcl_conversions.h>
/*#include <pcl/ros/conversions.h>
#include <pcl_ros/point_cloud.h>*/
// PCL 1.7 headers
//#include <pcl/point_cloud.h>
//#include <pcl/point_types.h>

// use "" for local headers
#include "I_SegmentedObjects.h"
#include "ParametersPoseEstimation.h"

// ROS generated headers
// typically these ones are generated at compilation time, 
// for instance, for a service called ClassParameterSetting.srv 
// within pose_estimation_uibk, we need to include this 
// #include "pose_estimation_uibk/ClassParameterSetting.h" 


namespace pose_estimation_uibk 
{

// use a name for the node and a verb it is suppose to do, Publisher, Server, etc...
class PoseEstimator
{
  private:

    // suggested/quasi_mandatory members, note the member_name_ naming convention

    // the node handle
    ros::NodeHandle nh_;

    // Node handle in the private namespace
    ros::NodeHandle priv_nh_;

    // services
    ros::ServiceServer srv_estimate_poses_;
    
    ros::ServiceClient srv_kinect;
    ros::Publisher pub_rec_scene_;
    
    Eigen::Matrix4f kinectToRobot;
    
  //  ros::Publisher pub_pcd;
    double offset_x,offset_y;
    sensor_msgs::PointCloud2 rec_scene_cloud;

  public:

    // callback functions
    bool estimatePoses(definitions::PoseEstimation::Request& request, definitions::PoseEstimation::Response& response);

    //Utility functions
    void poseEigenToMsg(const Eigen::Affine3d&, geometry_msgs::Pose&);

    void callback_kinect(const sensor_msgs::PointCloud2ConstPtr & input);

    // constructor
    PoseEstimator(ros::NodeHandle nh) : nh_(nh), priv_nh_("~")
    {   
        pub_rec_scene_ = nh_.advertise<sensor_msgs::PointCloud2>(nh_.resolveName("/pose_estimation_uibk/reconstructed_scene"), 10);

        // advertise service
        srv_estimate_poses_ = nh_.advertiseService(nh_.resolveName("/pose_estimation_uibk/estimate_poses"), &PoseEstimator::estimatePoses, this);
//	pub_pcd = nh_.advertise<pcl::PointCloud<pcl::PointXYZ> >(nh_.resolveName("/pose_estimation_uibk/object"), 1);
      /*  kinectToRobot <<
       -0.12718, -0.990185, 0.0579525,  0.559081,
         -0.760843, 0.0599052, -0.646166,  0.168621,
         0.636352, -0.126272, -0.760994,  0.946187,
        0,         0,         0,         1;*/
       kinectToRobot <<
 -0.065648,  -0.996571 , 0.0503785,   0.645214,
 -0.644791, 0.00383551,   -0.76435,   0.332272,
  0.761535, -0.0826616 , -0.642831 ,  0.714251,
         0,          0 ,         0,          1;        

    //  offset_x = 0.22;     
   /*    offset_y = 0.055; // can be 0 -- -0.03
       offset_x = 0.245;  // can 3cm more --- 0.22*/
       offset_x = 0;
       offset_y = 0;
       rec_scene_cloud.header.frame_id = "/world_link"; 
       rec_scene_cloud.header.seq = 0;
    }

    //! Empty stub
    ~PoseEstimator() {}
    void publish_scene();
    vector<int> give_object_ids(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud,pcl::PointCloud<pcl::PointXYZ>::Ptr obj_cloud);
};


bool PoseEstimator::estimatePoses(definitions::PoseEstimation::Request& request, definitions::PoseEstimation::Response& response)
{   
    ROS_INFO("Pose estimation service has been reached..."); 
    ros::ServiceClient client = nh_.serviceClient<definitions::KinectGrabberService>("/kinect_grabber/kinect_grab_name");
    definitions::KinectGrabberService srv;
    srv.request.wake_up = "";
    pcl::PointCloud<pcl::PointXYZ>::Ptr xyz_points_ (new pcl::PointCloud<pcl::PointXYZ>());
 
    if( !client.call(srv))
    {   
        cout << "Error for calling grab_kinect service" << endl;
        return false;
    }   

    ros::Duration(0.5).sleep();  
    string filename_ = srv.response.path_to_pclfile;
       
    cout << "service call succeeded , " << filename_ << endl;
    if (pcl::io::loadPCDFile<pcl::PointXYZ> (filename_, *xyz_points_) == -1) //* load the file
    {
        cout << "Couldn't read file: " << filename_ << endl;
        return false;
    }

    ROS_INFO("Pose estimation service has been called...");
    
    ROS_INFO("Initializing...");
    string filename; 
    filename = "/home/pacman/poseEstimation/parametersFiles/config.txt";
    ParametersPoseEstimation params(filename);
    I_SegmentedObjects objects;

    ROS_INFO("Recognizing poses...");
    params.recognizePose(objects,xyz_points_);
    
    // getting the result of pose estimation

    std::vector<string> names = objects.getObjects();
    std::vector<int> orderedList = objects.getIdsObjects(objects.getHeightList());
    boost::shared_ptr<vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > > transforms = objects.getTransforms ();
    
    std::vector<definitions::Object> detected_objects(names.size());

    int j;
    //Transform these types to ros message types

    /*pcl::PointCloud<pcl::PointXYZ>::Ptr xyz_points_trans (new pcl::PointCloud<pcl::PointXYZ>());
         Eigen::Matrix4d md_(kinectToRobot.inverse().cast<double>());
        Eigen::Affine3d e_ = Eigen::Affine3d(md_);   
    pcl::transformPointCloud(*xyz_points_,*xyz_points_trans,e_); 
     pcl::io::savePCDFileASCII ("/tmp/points_trans.pcd",*xyz_points_trans);*/
    for (int i = 0; i < names.size (); i++)
    {     
        j = orderedList[i]; 
        std::cout << j << std::endl;

        Eigen::Matrix4f transform_matrix = kinectToRobot.inverse()*transforms->at(j);
	//Eigen::Matrix4f transform_matrix = kinectToRobot*transforms->at(j);
	std::cout << "transformation matrix: " << kinectToRobot.inverse() << std::endl;

        std::cout <<    names.at(j) << std::endl;
        std::cout << transform_matrix << std::endl;

        Eigen::Matrix4d md(transform_matrix.cast<double>());
        Eigen::Affine3d e = Eigen::Affine3d(md);

        geometry_msgs::Pose current_pose;
        poseEigenToMsg(e, current_pose);
        
	// ** add offset ** //
	 current_pose.position.x += offset_x;
	current_pose.position.y += offset_y;
	
        definitions::Object object;
        object.pose = current_pose;
        object.name.data = names.at(j);
        detected_objects[i] = object;
	
/*	if( i == 0 ){
	  vector<pcl::PointCloud<pcl::PointXYZ>::Ptr> pcds = objects.getPointClouds();
	  pub_pcd.publish(*pcds[j]);
	}*/
    }
    
    response.detected_objects = detected_objects;

   /*  pcl::PointCloud<pcl::PointXYZ>::Ptr xyz_objs_ (new pcl::PointCloud<pcl::PointXYZ>());
     xyz_objs_->width = 640; xyz_objs_->height = 480;
     xyz_objs_->points.resize(xyz_points_->width*xyz_points_->height);
     for( size_t i = 0; i < xyz_objs_->points.size(); i++ )
     {
        pcl::PointXYZ point; point.x = std::numeric_limits<double>::quiet_NaN(); 
        point.y = std::numeric_limits<double>::quiet_NaN(); 
        point.z = std::numeric_limits<double>::quiet_NaN();
        xyz_objs_->points[i] = point;
     }
     for( int i = 0; i < names.size(); i++ )
     {
        vector<int> id_cur = give_object_ids(xyz_points_,objects.getPointCloudAt(i));
        for( size_t j = 0; j < id_cur.size(); j++ )
            xyz_objs_->points[id_cur[j]] = xyz_points_->points[id_cur[j]];
     }
      

     Eigen::Matrix4d md_(kinectToRobot.inverse().cast<double>());
     Eigen::Quaternion<double> quat(md_.block<3,3>(0,0));
     cout << "Quaternion: " << quat.x() << " "<<quat.y() << " "<< quat.z() << " "<<quat.w() << endl;
     Eigen::Quaternion<float> quat_inv(kinectToRobot.block<3,3>(0,0));
     cout << "Quaternion inv: " << quat_inv.x() << " "<<quat_inv.y() << " "<< quat_inv.z() << " "<<quat_inv.w() << endl;
     Eigen::Affine3d e_ = Eigen::Affine3d(md_);  
     pcl::transformPointCloud(*xyz_objs_,*xyz_objs_,e_);

     rec_scene_cloud.width = xyz_objs_->width;  rec_scene_cloud.height = xyz_objs_->height;
     rec_scene_cloud.data.resize(rec_scene_cloud.width*rec_scene_cloud.height);
     rec_scene_cloud.is_dense = false;
     pcl::toROSMsg(*xyz_objs_,rec_scene_cloud);
     rec_scene_cloud.header.frame_id = "/world_link";
     rec_scene_cloud.header.stamp = ros::Time::now();
    // rec_scene_cloud.header.seq ++;
    pub_rec_scene_.publish(rec_scene_cloud);*/
    ROS_INFO("Pose estimation service finisihed, and ready for another service requests...");
    return true;
}

vector<int> PoseEstimator::give_object_ids(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud,pcl::PointCloud<pcl::PointXYZ>::Ptr obj_cloud)
{
    vector<int> ids;
     pcl::KdTreeFLANN<pcl::PointXYZ> kdtree;
     int K = 1;
   
     kdtree.setInputCloud (cloud);
     vector<int> pointIdxNKNSearch(K);
     vector<float> pointNKNSquaredDistance(K);
     vector<float> neighborsDistances;

     pcl::PointXYZ searchPoint;

     for (size_t i = 0; i < obj_cloud->points.size (); ++i)
     {
       searchPoint= obj_cloud->points[i];
   
      if ( kdtree.nearestKSearch (searchPoint, K, pointIdxNKNSearch, pointNKNSquaredDistance) > 0 )
         ids.push_back(pointIdxNKNSearch[0]);           
     }
    return ids;
}
void PoseEstimator::publish_scene()
{
    rec_scene_cloud.header.frame_id = "/world_link";
    rec_scene_cloud.header.stamp = ros::Time::now();
    //rec_scene_cloud.header.seq ++;
    pub_rec_scene_.publish(rec_scene_cloud);
}
void PoseEstimator::poseEigenToMsg(const Eigen::Affine3d &e, geometry_msgs::Pose &m)
{
    m.position.x = e.translation()[0];
    m.position.y = e.translation()[1];
    m.position.z = e.translation()[2];
    Eigen::Quaterniond q = (Eigen::Quaterniond)e.linear();
    m.orientation.x = q.x();
    m.orientation.y = q.y();
    m.orientation.z = q.z();
    m.orientation.w = q.w();
    if (m.orientation.w < 0) 
    {
        m.orientation.x *= -1;
        m.orientation.y *= -1;
        m.orientation.z *= -1;
        m.orientation.w *= -1;
    }
}

} // namespace pose_estimation_uibk

int main(int argc, char **argv)
{
    ros::init(argc, argv, "pose_estimation_uibk_node");
    ros::NodeHandle nh;

    pose_estimation_uibk::PoseEstimator node(nh);
    ROS_INFO("Pose estimation node ready for service requests...");
   // ros::Rate loop_rate(30);
    while(ros::ok())
    {
        ros::spinOnce();
        /*node.publish_scene();
        loop_rate.sleep();*/
    }

    return 0;
}