#include "dji_camera/dji_camera.h"

// OpenCV includes
#include "opencv2/imgproc/imgproc.hpp"

// ROS includes
#include "camera_info_manager/camera_info_manager.h"

// STD includes
#include <cstring>

dji_camera::dji_camera(ros::NodeHandle& nh, image_transport::ImageTransport& imageT)
{
	// Load camera parameters
	if (!loadCameraInfo())
		ROS_ERROR("Camera calibration file could not be found");

	// Initialize DJI camera
	mode = GETBUFFER_MODE | TRANSFER_MODE;
	int ret = manifold_cam_init(mode);

	if(ret == -1)
		ROS_ERROR("Camera could not be initialized");

	// Set up Publishers
	imagePub = imageT.advertise("dji_sdk/image_raw", 1);
	cameraInfoPub = nh.advertise<sensor_msgs::CameraInfo>("dji_sdk/dji_camera_info", 10);

	// Initialize variables
	nCount = 0;
	imageWidth = 1280;
	imageHeight = 720;
	imageChannels = 3;
	frameSize = imageWidth * imageHeight * imageChannels / 2;
}

dji_camera::~dji_camera()
{
	// Make sure camera exits properly otherwise the connection will appear occupied
	while(!manifold_cam_exit())
		sleep(1);
}

bool dji_camera::loadCameraInfo()
{
	// Create private nodeHandle to load the camera info
	ros::NodeHandle nh_private("~");

	// Initialize parameters
	std::string camera_name;
	std::string camera_info_url;	

	nh_private.param("is_mono", is_mono, 1);
	nh_private.param("camera_name", camera_name, std::string("camera_dji"));
	nh_private.param("camera_info_url", camera_info_url,
		std::string("package://dji_camera/calibration_files/zenmuse_z3.yaml"));

	// Create camera info manager
	camera_info_manager::CameraInfoManager camInfoMngr(nh_private, camera_name, camera_info_url);

	if (camInfoMngr.loadCameraInfo(camera_info_url))
	{
		cam_info = camInfoMngr.getCameraInfo();
		ROS_INFO("Camera calibration file loaded");
	}
	else
		return false;

	return true;
}

bool dji_camera::grabFrame()
{
	// Check if new frame is available
	unsigned char buffer[frameSize];
	unsigned int nFrame;
	int ret = manifold_cam_read(buffer, &nFrame, 1);

	if(ret == -1)
		return false;
	else
	{
		// Create openCV Mat to store the image
		cv::Mat frameYcbCr = cv::Mat(imageHeight * 3 / 2, imageWidth, CV_8UC1, buffer);
		cv::Mat frameBGRGray;

		// If grayscale image is selected
		if (is_mono)
			cv::cvtColor(frameYcbCr, frameBGRGray, CV_YUV2GRAY_NV12);
		// If coloured image is selected
		else
			cv::cvtColor(frameYcbCr, frameBGRGray, CV_YUV2BGR_NV12);

		rosMat.image = frameBGRGray;
	}
	return true;
}

bool dji_camera::publishAll()
{
	// Check if new frame was captured
	if(grabFrame())
	{
		// Get time
		ros::Time time = ros::Time::now();

		// Setup image message
		sensor_msgs::Image rosImage;

		if(is_mono)
			rosMat.encoding = "mono8";
		else
			rosMat.encoding = "bgr8";

		rosMat.header.stamp = time;
		rosMat.header.frame_id = "image";

		rosMat.toImageMsg(rosImage);

		// Setup camera info message
		cam_info.header.stamp = time;
		cam_info.header.seq = nCount;

		// Publish everything
		imagePub.publish(rosImage);
		cameraInfoPub.publish(cam_info);

		nCount++;

		return true;
	}
	else
		return false;
}

////////////////////////////////////////////////////////////
////////////////////////  Main  ////////////////////////////
////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
  ros::init(argc, argv, "dji_camera");
  ros::NodeHandle nh;
  image_transport::ImageTransport imageT(nh);

  dji_camera manifoldCamera(nh, imageT);

  ros::Rate rate(10);

  while(ros::ok() && !manifold_cam_exit())
  {
  	ros::spinOnce();

  	if(!manifoldCamera.publishAll())
  	{
  		ROS_ERROR("Could not retrieve new frame");
  		break;
  	}

  	rate.sleep();
  }

  return 0;
}