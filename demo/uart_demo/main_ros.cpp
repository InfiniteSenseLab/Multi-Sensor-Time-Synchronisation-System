#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <sensor_msgs/Imu.h>

#include "cam_manager.h"
#include "serial_manager.h"
#include "data_manager.h"

inline ros::Time CreateRosTimestamp(const uint64_t mico_sec) {
  uint32_t nsec_per_second = 1e9;
  auto u64 = mico_sec * 1000;
  uint32_t sec = u64 / nsec_per_second;
  uint32_t nsec = u64 - (sec * nsec_per_second);
  return {sec, nsec};
}

void PublishIMUData(const ros::Publisher& pub, const ImuData& imudata) {
  sensor_msgs::Imu imu_msg_data;
  imu_msg_data.header.frame_id = "/base_imu_link";
  imu_msg_data.header.stamp = CreateRosTimestamp(imudata.time_stamp_us);

  imu_msg_data.angular_velocity.x = imudata.gx;
  imu_msg_data.angular_velocity.y = imudata.gy;
  imu_msg_data.angular_velocity.z = imudata.gz;

  imu_msg_data.linear_acceleration.x = imudata.ax;
  imu_msg_data.linear_acceleration.y = imudata.ay;
  imu_msg_data.linear_acceleration.z = imudata.az;

  pub.publish(imu_msg_data);
}
void SigIntHandler(int sig) {
  ros::shutdown();  // 让ROS节点安全退出
}
int main(int argc, char** argv) {
  std::this_thread::sleep_for(std::chrono::milliseconds(5000));
  // ROS 初始化
  ros::init(argc, argv, "uart_demo", ros::init_options::NoSigintHandler);
  ros::NodeHandle node;
  // 串口线程初始化
  auto serial_manager = std::make_shared<SerialManager>("/dev/ttyACM0");
  serial_manager->Start();

  // IMU数据发布
  ros::Publisher imu_pub = node.advertise<sensor_msgs::Imu>("/imu_sync_board", 1000);

  // 相机线程初始化
  CamManger::GetInstance().Initialization();
  CamManger::GetInstance().Start();

  // 等待相机启动
  std::this_thread::sleep_for(std::chrono::milliseconds(5000));

  // 遍历所有相机，新建发布者
  image_transport::ImageTransport it(node);
  std::vector<std::string> all_cam_names;
  DataManger::GetInstance().GetAllCamNames(all_cam_names);
  std::cout << "Number of cameras detected " << all_cam_names.size() << std::endl;
  std::map<std::string, image_transport::Publisher> pub_list;
  for (auto& cam : all_cam_names) {
    pub_list[cam] = it.advertise(cam, 30);
  }

  // ROS循环发布传感器数据
  ros::Rate loop_rate(1000);
  ImuData imudata{};

  while (node.ok()) {
    // 获取IMU数据
    while (DataManger::GetInstance().GetNewImuData(imudata)) {
      // 发布IMU数据
      PublishIMUData(imu_pub, imudata);
    }
    // 遍历所有相机，发布图像数据
    ImgData img_data{};
    for (auto& cam : all_cam_names) {
      // 获取图像数据
      while (DataManger::GetInstance().GetNewCamData(cam, img_data)) {
        // 发布图像数据
        sensor_msgs::ImagePtr msg =
            // mono8:灰度类型,bgr8:彩图，具体需要根据相机类型进行修改
            cv_bridge::CvImage(std_msgs::Header(), "mono8", img_data.image.clone()).toImageMsg();
        msg->header.stamp = CreateRosTimestamp(img_data.time_stamp_us);
                pub_list[cam].publish(msg);
            }
        }
        loop_rate.sleep();
    }
    // 释放资源
    serial_manager->Stop();
    CamManger::GetInstance().Stop();
    return 0;
}
