/*
 * Copyright (C) 2019-2021 LEIDOS.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

#include "health_monitor.h"
#include <sstream>
#include "carma_utils/CARMANodeHandle.h"


namespace health_monitor
{

    HealthMonitor::HealthMonitor()
    {
        car_ = false;
        truck_ = false;
    }
    void HealthMonitor::initialize()
    {
        // init node handles
        ROS_INFO_STREAM("Initalizing health_monitor node...");
        pnh_.reset(new ros::CARMANodeHandle("~"));
        nh_.reset(new ros::CARMANodeHandle());
        // init ros service servers, publishers and subscribers
        driver_discovery_subscriber_ = nh_->subscribe<cav_msgs::DriverStatus>("driver_discovery", 5, &HealthMonitor::driver_discovery_cb, this);

        // load params
        spin_rate_ = pnh_->param<double>("spin_rate_hz", 10.0);
        driver_timeout_ = pnh_->param<double>("required_driver_timeout", 500);
        startup_duration_ = pnh_->param<double>("startup_duration", 25);
        
        pnh_->getParam("required_drivers", required_drivers_);
        pnh_->getParam("lidar_gps_drivers", lidar_gps_drivers_);
        pnh_->getParam("camera_drivers",camera_drivers_);

        truck_=false;
        car_=false;
        pnh_->getParam("truck", truck_);
        pnh_->getParam("car", car_);

        // Log parameters
        ROS_INFO_STREAM("Health Monitor Parameters {");
        ROS_INFO_STREAM("spin_rate_hz: " << spin_rate_);
        ROS_INFO_STREAM("required_driver_timeout: " << driver_timeout_);
        ROS_INFO_STREAM("startup_duration: " << startup_duration_);
        ROS_INFO_STREAM("truck: " << truck_);
        ROS_INFO_STREAM("car: " << car_);

        ROS_INFO_STREAM("required_drivers: [");
        for(auto p : required_drivers_) {
            ROS_INFO_STREAM("   " << p);
        }
        ROS_INFO_STREAM("  ]");

        ROS_INFO_STREAM("lidar_gps_drivers: [");
        for(auto p : lidar_gps_drivers_) {
            ROS_INFO_STREAM("   " << p);
        }
        ROS_INFO_STREAM("  ]");

        ROS_INFO_STREAM("camera_drivers: [");
        for(auto p : camera_drivers_) {
            ROS_INFO_STREAM("   " << p);
        }

        ROS_INFO_STREAM("  ]");
        ROS_INFO_STREAM("}");
        
         

        // initialize worker class
        driver_manager_ = DriverManager(required_drivers_, driver_timeout_,lidar_gps_drivers_,camera_drivers_); 

        // record starup time
        start_up_timestamp_ = ros::Time::now().toNSec() / 1e6;

        ros::CARMANodeHandle::setSystemAlertCallback([&](const cav_msgs::SystemAlertConstPtr& msg) -> void {

            bool shutdown = false;


            if (msg->type == cav_msgs::SystemAlert::FATAL)
            {
                // An empty source_node indicates that the alert was generated by a ROS1 node
                // Therefore there is no complex error handling and the system should shutdown
                if(msg->source_node.empty())
                {
                    shutdown = true;
                }
                // If the ROS2 system controller failed we need to shutdown
                else if (msg->source_node.compare("/system_controller") == 0) 
                {
                    shutdown = true;
                }
                else { // No need to shutdown if the alert was generated by a ROS2 node and it wasn't the system controller
                    shutdown = false;
                }
            }
            

            if (shutdown)
            { 
                ROS_ERROR_STREAM("Health Monitor shutting down due to system alert: " << msg->description);
                std::string header = "health_monitor requesting shutdown due to: " + msg->description;

                cav_msgs::SystemAlert new_msg;
                new_msg.description = header;
                new_msg.type = cav_msgs::SystemAlert::SHUTDOWN;

                nh_->publishSystemAlert(new_msg);
            }

        });
        

        ros::CARMANodeHandle::setExceptionCallback([&](const std::exception& exp) -> void {

            cav_msgs::SystemAlert new_msg;
            new_msg.type = cav_msgs::SystemAlert::SHUTDOWN;
            new_msg.description = exp.what();

            nh_->publishSystemAlert(new_msg);

        });

    }
    
    void HealthMonitor::run()
    {
        initialize();
        ros::CARMANodeHandle::setSpinRate(spin_rate_);
        ros::CARMANodeHandle::setSpinCallback(std::bind(&HealthMonitor::spin_cb, this));
        ros::CARMANodeHandle::spin();
    }


    void HealthMonitor::driver_discovery_cb(const cav_msgs::DriverStatusConstPtr& msg)
    {
        // convert ros nanosecond to millisecond by the factor of 1/1e6
        driver_manager_.update_driver_status(msg, ros::Time::now().toNSec() / 1e6);
    }

    bool HealthMonitor::spin_cb()
    {
        long time_now=(ros::Time::now().toNSec() / 1e6);
        ros::Duration sd(startup_duration_);
        long start_duration=sd.toNSec() / 1e6;

        auto dm = driver_manager_.handleSpin(truck_,car_,time_now,start_up_timestamp_,start_duration);
        if (!prev_alert) {
            prev_alert = dm;
            nh_->publishSystemAlert(dm);
        } else if (prev_alert->type == dm.type && prev_alert->description.compare(dm.description) == 0) { // Do not publish duplicate alerts
            ROS_DEBUG_STREAM("No change to alert status");
        } else {
            prev_alert = dm;
            nh_->publishSystemAlert(dm);
        }
    
        return true;
    }


    void HealthMonitor::setDriverManager(DriverManager dm)
    {
        driver_manager_ = dm;
    }

    void HealthMonitor::setCarTrue()
    {
        car_ = true;
        if(truck_ == true)
            throw std::invalid_argument("truck_ = true");
    }

    void HealthMonitor::setTruckTrue()
    {
        truck_ = true;
        if(car_ == true)
            throw std::invalid_argument("car_ = true");
        
    }


}