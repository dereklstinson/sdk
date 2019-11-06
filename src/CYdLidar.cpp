﻿/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2015-2019, YDLIDAR Team. (http://www.ydlidar.com).
*  Copyright (c) 2015-2018, EAIBOT Co., Ltd. (http://www.eaibot.com)
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/
#include "CYdLidar.h"
#include "common.h"
#include <map>
#include <angles.h>
#include <elog_file_cfg.h>
#include <elog_file.h>

using namespace std;
using namespace ydlidar;
using namespace impl;
using namespace angles;


/*-------------------------------------------------------------
						Constructor
-------------------------------------------------------------*/
CYdLidar::CYdLidar(): lidarPtr(nullptr) {
  m_SerialPort        = "";
  m_SerialBaudrate    = 230400;
  m_FixedResolution   = true;
  m_Reversion         = true;
  m_AutoReconnect     = true;
  m_MaxAngle          = 180.f;
  m_MinAngle          = -180.f;
  m_MaxRange          = 16.0;
  m_MinRange          = 0.08;
  m_SampleRate        = 9;
  m_ScanFrequency     = 10;
  isScanning          = false;
  node_counts         = 1200;
  each_angle          = 0.3;
  frequencyOffset     = 0.4;
  m_AbnormalCheckCount  = 4;
  Major               = 0;
  Minjor              = 0;
  m_IgnoreArray.clear();
  node_duration = 1e9 / 9000;
  m_OffsetTime = 0.0;
  last_node_time = getTime();
  nodes = new node_info[YDlidarDriver::MAX_SCAN_NODES];
  last_nodes.resize(2);
  last_count.resize(2);

  for (int i = 0; i < 2; i++) {
    last_nodes[i] = new node_info[YDlidarDriver::MAX_SCAN_NODES];
    last_count[i] = YDlidarDriver::MAX_SCAN_NODES;
  }

  last_index = 0;

  m_FilterNoise = true;
  unique_range[16000] = true;
  unique_range[16210] = true;
  unique_range[16220] = true;
  unique_range[16300] = true;
  unique_range[16310] = true;
  unique_range[16320] = true;
  unique_range[16330] = true;
  unique_range[16340] = true;


  multi_range[16100] = true;
  multi_range[16110] = true;
  multi_range[16120] = true;
  multi_range[16130] = true;
  multi_range[16200] = true;

  smaller_range[16000] = true;

  ini.SetUnicode();
  m_CalibrationFileName = "laserconfig.ini";
  m_AngleOffset = 0.0;
  m_isAngleOffsetCorrected = false;
  m_StartAngleOffset = false;
  m_RobotLidarDifference = 0;
  last_frequency = 0.0;

  indices.clear();
  bearings.clear();
  range_data.clear();
  m_serial_number.clear();

#ifdef YDDEBUG
  elog_file_init();
#endif

}

/*-------------------------------------------------------------
                    ~CYdLidar
-------------------------------------------------------------*/
CYdLidar::~CYdLidar() {
  disconnecting();

  if (nodes) {
    delete[] nodes;
    nodes = nullptr;
  }

  for (int i = 0; i < 2; i++) {
    if (last_nodes[i]) {
      delete[] last_nodes[i];
      last_nodes[i] = nullptr;
    }
  }



#ifdef YDDEBUG
  elog_file_close();
#endif
}

void CYdLidar::disconnecting() {
  if (lidarPtr) {
    lidarPtr->disconnect();
    delete lidarPtr;
    lidarPtr = nullptr;
  }

  isScanning = false;
}

void CYdLidar::elog(const string &msg) {

#ifdef YDDEBUG
  elog_file_write(msg.c_str(), msg.size());
#endif
}

std::map<std::string, std::string>  CYdLidar::lidarPortList() {
  return ydlidar::YDlidarDriver::lidarPortList();
}

//获取零位修正值
float CYdLidar::getAngleOffset() const {
  return m_AngleOffset;
}

//判断零位角度是否修正
bool CYdLidar::isAngleOffetCorrected() const {
  return m_isAngleOffsetCorrected && !m_StartAngleOffset;
}

void CYdLidar::saveNoiseDataToFile() {
  uint64_t start_time = getTime();

  if (lidarPtr) {
    lidarPtr->saveNoiseDataToFile();
  }

  elog("Save Noise Data To File.......\n\n");
  elog(format("last_index: %d, m_AngleOffset: %f\n\n", last_index,
              m_AngleOffset));

  for (int i = 0; i < 2; i++) {
    handleLaserData(last_nodes[i], last_count[i]);
    elog(format("Save Noise Data[%d] is Finished.......\n\n", i));
  }

  elog(format("save time: %fms\n",
              static_cast<float>((getTime() - start_time) / 1e6)));
  /* mv xxx.log.n-1 => xxx.log.n, and xxx.log => xxx.log.0 */
  int n;
  char oldpath[256], newpath[256];
  size_t base = strlen(ELOG_FILE_NAME);

  memcpy(oldpath, ELOG_FILE_NAME, base);
  memcpy(newpath, ELOG_FILE_NAME, base);

  for (n = ELOG_FILE_MAX_ROTATE - 1; n >= 0; --n) {
    snprintf(oldpath + base, PATH_SUFFIX_LEN, n ? ".%d" : "", n - 1);
    snprintf(newpath + base, PATH_SUFFIX_LEN, n ? ".%d.back" : ".back", n);

    if (fileExists(oldpath)) {
      system(format("cp %s %s", oldpath, newpath).c_str());
    }
  }

  snprintf(oldpath + base, PATH_SUFFIX_LEN, ".%d", 0);
  snprintf(newpath + base, PATH_SUFFIX_LEN, ".%d.back", 0);

  if (fileExists(oldpath)) {
    system(format("cp %s %s", oldpath, newpath).c_str());
  }


}

void CYdLidar::handleLaserData(node_info *data, size_t count) {
  float range = 0.0;
  float angle = 0.0;
  std::string buffer;
  std::map<int, bool> filter_flag;

  for (int i = 0; i < count; i++) {
    angle = (float)((data[i].angle_q6_checkbit >>
                     LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) / 64.0f) + m_AngleOffset;
    range = (float)data[i].distance_q2 / 4.f;
    uint16_t distance = data[i].distance_q2 >>
                        LIDAR_RESP_MEASUREMENT_DISTANCE_SHIFT;

    angle = angles::from_degrees(angle);

    double AngleCorrectForDistance = 0.0;
    double LastAngleCorrectForDistance = 0.0;

    if (data[i].distance_q2 != 0) {
      AngleCorrectForDistance = atan(((21.8 * (155.3 - range)) / 155.3) / range);
      LastAngleCorrectForDistance = AngleCorrectForDistance;
    }

    if (m_FilterNoise) {
      buffer +=
        (format("[%d][F]a: %f, d: %f, c: %f, f: %d\n",
                i,
                angles::to_degrees(angle),
                range,
                angles::to_degrees(AngleCorrectForDistance), 0));
    }

    AngleCorrectForDistance = 0.0;

    if (m_FilterNoise) {
      auto find = filter_flag.find(i);

      if (find == filter_flag.end()) {
        filter_flag[i] = false;
      }

      //filter pinpang
      if (range < 1000) {
        bool filter = true;

        for (int j = i + 1; (j < count && j < i + 4); j++) {
          uint16_t drange = data[j].distance_q2 >> LIDAR_RESP_MEASUREMENT_DISTANCE_SHIFT;

          if (multi_range.find(drange) != multi_range.end() ||
              smaller_range.find(drange) != smaller_range.end()) {
            filter = false;
            break;
          }
        }

        filter_flag[i] = filter;
      }

      if (unique_range.find(distance) != unique_range.end()) {
        if (unique_range[distance]) {
          filter_flag[i] = true;
        }
      }

      if (multi_range.find(distance) != multi_range.end()) {
        if (multi_range[distance]) {
          filter_flag[i] = true;
          int index = i;

          while (index >= 0 && index >= i - 3) {
            filter_flag[index] = true;
            index--;
          }

          if (i + 1 < count) {
            filter_flag[i + 1] = true;
          }

        }
      }

      if (i < 3 || i >= count - 3) {
        filter_flag[i] = true;
      }


      if (filter_flag[i]) {
        range = 0.0;
      }
    }

    if (range > 1) {
      AngleCorrectForDistance = LastAngleCorrectForDistance;
    }

    angle += AngleCorrectForDistance;

    if (m_FilterNoise) {
      buffer +=
        (format("[%d][B]a: %f, d: %f, c: %f, f: %d\n",
                i,
                angles::to_degrees(angle),
                range,
                angles::to_degrees(AngleCorrectForDistance), filter_flag[i]));
    }

  }

  elog(buffer);
}


/*-------------------------------------------------------------
						doProcessSimple
-------------------------------------------------------------*/
/*-------------------------------------------------------------
						doProcessSimple
-------------------------------------------------------------*/
bool  CYdLidar::doProcessSimple(LaserScan &outscan, bool &hardwareError) {
  hardwareError			= false;

  // Bound?
  if (!checkHardware()) {
    hardwareError = true;
    delay(1000 / (2 * m_ScanFrequency));
    return false;
  }

  size_t   count = YDlidarDriver::MAX_SCAN_NODES;

  //fit line
  //line feature
  indices.clear();
  bearings.clear();
  range_data.clear();

  //wait Scan data:
  uint64_t tim_scan_start = getTime();
  result_t op_result =  lidarPtr->grabScanData(nodes, count);
  uint64_t tim_scan_end = getTime();

  std::string buffer;
  buffer.clear();

  // Fill in scan data:
  if (IS_OK(op_result)) {
    last_count[last_index] = count;
    memcpy(last_nodes[last_index], nodes, count * sizeof(node_info));
    last_index++;
    last_index = last_index % 2;
    uint64_t scan_time = node_duration * (count - 1);
    tim_scan_end += m_OffsetTime * 1e9;
    tim_scan_end -= node_duration;
    tim_scan_start = tim_scan_end -  scan_time ;

    if (tim_scan_start - last_node_time > -2e6 &&
        tim_scan_start - last_node_time < 0) {
      tim_scan_start = last_node_time;
      tim_scan_end = tim_scan_start + scan_time;
    }

    last_node_time = tim_scan_end;

    if (m_MaxAngle < m_MinAngle) {
      float temp = m_MinAngle;
      m_MinAngle = m_MaxAngle;
      m_MaxAngle = temp;
    }

    int all_node_count = count;

    outscan.self_time_stamp = tim_scan_start;
    outscan.system_time_stamp = tim_scan_start;
    outscan.config.min_angle = angles::from_degrees(m_MinAngle);
    outscan.config.max_angle =  angles::from_degrees(m_MaxAngle);
    outscan.config.scan_time =  static_cast<float>(scan_time / 1e9);
    outscan.config.time_increment = outscan.config.scan_time / (double)count;
    outscan.config.min_range = m_MinRange;
    outscan.config.max_range = m_MaxRange;

    if (m_FixedResolution) {
      all_node_count = node_counts;
    }

    outscan.config.angle_increment = (outscan.config.max_angle -
                                      outscan.config.min_angle) / all_node_count;

    outscan.ranges.resize(all_node_count, 0);
    outscan.intensities.resize(all_node_count, 0);

    float range = 0.0;
    float intensity = 0.0;
    float angle = 0.0;
    int index = 0;
    std::map<int, bool> filter_flag;

    for (int i = 0; i < count; i++) {
      angle = (float)((nodes[i].angle_q6_checkbit >>
                       LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) / 64.0f) + m_AngleOffset;
      range = (float)nodes[i].distance_q2 / 4.f;
      uint16_t distance = nodes[i].distance_q2 >>
                          LIDAR_RESP_MEASUREMENT_DISTANCE_SHIFT;
      intensity = (float)(nodes[i].sync_quality);

      angle = angles::from_degrees(angle);

      double AngleCorrectForDistance = 0.0;
      double LastAngleCorrectForDistance = 0.0;

      if (nodes[i].distance_q2 != 0) {
        AngleCorrectForDistance = atan(((21.8 * (155.3 - range)) / 155.3) / range);
        LastAngleCorrectForDistance = AngleCorrectForDistance;
      }

      AngleCorrectForDistance = 0.0;

      if (m_FilterNoise) {
        auto find = filter_flag.find(i);

        if (find == filter_flag.end()) {
          filter_flag[i] = false;
        }

        //filter pinpang
        if (range < 1000) {
          bool filter = true;

          for (int j = i + 1; (j < count && j < i + 4); j++) {
            uint16_t drange = nodes[j].distance_q2 >> LIDAR_RESP_MEASUREMENT_DISTANCE_SHIFT;

            if (multi_range.find(drange) != multi_range.end() ||
                smaller_range.find(drange) != smaller_range.end()) {
              filter = false;
              break;
            }
          }

          filter_flag[i] = filter;
        }

        if (unique_range.find(distance) != unique_range.end()) {
          if (unique_range[distance]) {
            filter_flag[i] = true;
          }
        }

        if (multi_range.find(distance) != multi_range.end()) {
          if (multi_range[distance]) {
            filter_flag[i] = true;
            int index = i;

            while (index >= 0 && index >= i - 3) {
              filter_flag[index] = true;
              index--;
            }

            if (i + 1 < count) {
              filter_flag[i + 1] = true;
            }

          }
        }

        if (i < 3 || i >= count - 3) {
          filter_flag[i] = true;
        }


        if (filter_flag[i]) {
          range = 0.0;
          intensity = 0.0;
        }
      }

      if (range > 1) {
        AngleCorrectForDistance = LastAngleCorrectForDistance;
      }

      angle += AngleCorrectForDistance;

      if (m_Reversion) {
        angle = angle + M_PI;
      }

      //逆时针
      angle = 2 * M_PI - angle;
      angle = angles::normalize_angle(angle);

      for (uint16_t j = 0; j < m_IgnoreArray.size(); j = j + 2) {
        if ((angles::from_degrees(m_IgnoreArray[j]) <= angle) &&
            (angle <= angles::from_degrees(m_IgnoreArray[j + 1]))) {
          range = 0.0;
          break;
        }
      }

      if (range > 0.0) {
        range /= 1000.f;
      }

      if (range > m_MaxRange || range < m_MinRange) {
        range = 0.0;
        intensity = 0.0;
      }

      if (angle >= outscan.config.min_angle &&
          angle <= outscan.config.max_angle) {
        index = std::ceil((angle - outscan.config.min_angle) /
                          outscan.config.angle_increment);

        if (index >= 0 && index < all_node_count) {
          outscan.ranges[index] = range;
          outscan.intensities[index] = intensity;
        }

        //加入拟合直线数据
        if (m_StartAngleOffset && range >= m_MinRange) {
          bearings.push_back(angle);
          indices.push_back(indices.size());
          range_data.ranges.push_back(range);
          range_data.xs.push_back(cos(angle)*range);
          range_data.ys.push_back(sin(angle)*range);
        }

      }

    }

    if (fabs(last_frequency - 1.0 / outscan.config.scan_time) < 0.03) {
      fitLineSegment();
    }

    last_frequency = 1.0 / outscan.config.scan_time;
    return true;
  } else {
    if (IS_FAIL(op_result)) {
      // Error? Retry connection
    }
  }

  return false;

}

void CYdLidar::fitLineSegment() {
  //当设置启动零位修正，　开始拟合直线．
  if (m_StartAngleOffset) {
    line_feature_.setCachedRangeData(bearings, indices, range_data);
    std::vector<gline> glines;
    line_feature_.extractLines(glines);
    bool find_line_angle = false;
    gline max;
    max.distance = 0.0;
    double line_angle = 0.0;

    if (glines.size()) {
      max = glines[0];
    }

    float offsetAngle = 0.0;

    for (std::vector<gline>::const_iterator it = glines.begin();
         it != glines.end(); ++it) {
      line_angle = M_PI_2 - it->angle;
      line_angle -= M_PI;
      line_angle += angles::from_degrees(m_RobotLidarDifference);
      line_angle = angles::normalize_angle(line_angle);

      //可以再加入别的策略来约束，比如直线到中心点的距离
      //角度差小于Ｍ_PI/4的直线，才算修正直线
      //m_RobotLidarDifference 值表示雷达零度与机器人然零度之间的理论差值（0,180)四个值
      //如果雷达零度和机器人零度在一个方向，当前值设置位零，如果差90就是90　反方向就是180
      if (fabs(line_angle) < fabs(angles::from_degrees(m_RobotLidarDifference) -
                                  M_PI / 3)) {
        //直线距离大于1.0的直线才算修正直线
        if (it->distance > 1.0 && it->distance > max.distance) {
          max = (*it);
          find_line_angle = true;
//              line_angle -= angles::from_degrees(m_RobotLidarDifference);
          offsetAngle = -angles::to_degrees(line_angle);
        }
      }

    }

    //找到拟合的直线，　保存修正值到文件
    if (find_line_angle) {
      m_AngleOffset += offsetAngle;
      saveOffsetAngle();
    }
  }
}

/*-------------------------------------------------------------
						turnOn
-------------------------------------------------------------*/
bool  CYdLidar::turnOn() {
  if (isScanning && lidarPtr->isscanning()) {
    return true;
  }

  // start scan...
  result_t op_result = lidarPtr->startScan();

  if (!IS_OK(op_result)) {
    op_result = lidarPtr->startScan();

    if (!IS_OK(op_result)) {
      lidarPtr->stop();
      fprintf(stderr, "[CYdLidar] Failed to start scan mode: %x\n", op_result);
      elog(format("[CYdLidar] Failed to start scan mode: %x\n", op_result));
      isScanning = false;
      return false;
    }
  }

  if (checkLidarAbnormal()) {
    lidarPtr->stop();
    fprintf(stderr,
            "[CYdLidar] Failed to turn on the Lidar, because the lidar is blocked or the lidar hardware is faulty.\n");
    elog("[CYdLidar] Failed to turn on the Lidar, because the lidar is blocked or the lidar hardware is faulty.\n");
    isScanning = false;
    return false;
  }

  checkCalibrationAngle();

  node_duration = lidarPtr->getPointTime();
  isScanning = true;
  lidarPtr->setAutoReconnect(m_AutoReconnect);
  printf("[YDLIDAR INFO] Now YDLIDAR is scanning ......\n");
  elog("Now YDLIDAR is scanning ......\n");
  fflush(stdout);
  return true;
}

/*-------------------------------------------------------------
						turnOff
-------------------------------------------------------------*/
bool  CYdLidar::turnOff() {
  if (lidarPtr) {
    lidarPtr->stop();
  }

  if (isScanning) {
    printf("[YDLIDAR INFO] Now YDLIDAR Scanning has stopped ......\n");
  }

  isScanning = false;
  return true;
}

bool CYdLidar::checkLidarAbnormal() {
  size_t   count = YDlidarDriver::MAX_SCAN_NODES;
  int check_abnormal_count = 0;

  if (m_AbnormalCheckCount < 2) {
    m_AbnormalCheckCount = 2;
  }

  result_t op_result = RESULT_FAIL;

  while (check_abnormal_count < m_AbnormalCheckCount) {
    //Ensure that the voltage is insufficient or the motor resistance is high, causing an abnormality.
    if (check_abnormal_count > 0) {
      delay(check_abnormal_count * 1000);
    }

    count = YDlidarDriver::MAX_SCAN_NODES;
    op_result =  lidarPtr->grabScanData(nodes, count);

    if (IS_OK(op_result)) {
      return false;
    }

    check_abnormal_count++;
  }

  return !IS_OK(op_result);
}

/** Returns true if the device is connected & operative */
bool CYdLidar::getDeviceHealth() {
  if (!lidarPtr) {
    return false;
  }

  lidarPtr->stop();
  result_t op_result;
  device_health healthinfo;
  printf("[YDLIDAR]:SDK Version: %s\n", YDlidarDriver::getSDKVersion().c_str());
  elog(format(("[YDLIDAR]:SDK Version: %s\n",
               YDlidarDriver::getSDKVersion().c_str())));
  op_result = lidarPtr->getHealth(healthinfo);

  if (IS_OK(op_result)) {
    printf("[YDLIDAR]:Lidar running correctly ! The health status: %s\n",
           (int)healthinfo.status == 0 ? "good" : "bad");

    if (healthinfo.status == 2) {
      fprintf(stderr,
              "Error, Yd Lidar internal error detected. Please reboot the device to retry.\n");
      return false;
    } else {
      return true;
    }

  } else {
    fprintf(stderr, "Error, cannot retrieve Yd Lidar health code: %x\n", op_result);
    return false;
  }

}

bool CYdLidar::getDeviceInfo() {
  if (!lidarPtr) {
    return false;
  }

  device_info devinfo;
  result_t op_result = lidarPtr->getDeviceInfo(devinfo);

  if (!IS_OK(op_result)) {
    fprintf(stderr, "get Device Information Error\n");
    return false;
  }

  if (devinfo.model != YDlidarDriver::YDLIDAR_G4 &&
      devinfo.model != YDlidarDriver::YDLIDAR_G4PRO &&
      devinfo.model != YDlidarDriver::YDLIDAR_G1) {
    printf("[YDLIDAR INFO] Current SDK does not support current lidar models[%d]\n",
           devinfo.model);
    return false;
  }

  std::string model = "G4";

  switch (devinfo.model) {
    case YDlidarDriver::YDLIDAR_G4:
      frequencyOffset = 0.4;
      model = "G4";
      break;

    case YDlidarDriver::YDLIDAR_G4PRO:
      model = "G4Pro";
      frequencyOffset = 0.0;
      break;

    default:
      break;
  }

  m_serial_number.clear();
  Major = (uint8_t)(devinfo.firmware_version >> 8);
  Minjor = (uint8_t)(devinfo.firmware_version & 0xff);
  printf("[YDLIDAR] Connection established in [%s][%d]:\n"
         "Firmware version: %u.%u\n"
         "Hardware version: %u\n"
         "Model: %s\n"
         "Serial: ",
         m_SerialPort.c_str(),
         m_SerialBaudrate,
         Major,
         Minjor,
         (unsigned int)devinfo.hardware_version,
         model.c_str());

  for (int i = 0; i < 16; i++) {
    printf("%01X", devinfo.serialnum[i] & 0xff);
    m_serial_number += format("%01X", devinfo.serialnum[i] & 0xff);
  }

  printf("\n");
  elog(format("[YDLIDAR] Connection established in [%s][%d]:\n"
              "Firmware version: %u.%u\n"
              "Hardware version: %u\n"
              "Model: %s\n"
              "Serial: %s\n",
              m_SerialPort.c_str(),
              m_SerialBaudrate,
              Major,
              Minjor,
              (unsigned int)devinfo.hardware_version,
              model.c_str(),
              m_serial_number.c_str()));

  checkLidarFilter();
  checkSampleRate();
  printf("[YDLIDAR INFO] Current Sampling Rate : %dK\n", m_SampleRate);
  elog(format("[YDLIDAR INFO] Current Sampling Rate : %dK\n", m_SampleRate));
  checkScanFrequency();
  return true;
}

void CYdLidar::checkLidarFilter() {
  if (Major < 2 || (Major == 2 && Minjor < 34)) {
    m_FilterNoise = false;
  }

  printf("[YDLIDAR INFO] Current FilterNoise Flag: %s\n",
         m_FilterNoise ? "true" : "false");
  elog(format("[YDLIDAR INFO] Current FilterNoise Flag: %s\n",
              m_FilterNoise ? "true" : "false"));
}

void CYdLidar::checkSampleRate() {
  sampling_rate _rate;
  int _samp_rate = 9;
  int try_count = 0;
  int success_count = 0;
  node_counts = 1200;
  each_angle = 0.3;
  result_t ans = lidarPtr->getSamplingRate(_rate);

  if (IS_OK(ans)) {
    switch (m_SampleRate) {
      case 4:
        _samp_rate = YDlidarDriver::YDLIDAR_RATE_4K;
        break;

      case 8:
        _samp_rate = YDlidarDriver::YDLIDAR_RATE_8K;
        break;

      case 9:
        _samp_rate = YDlidarDriver::YDLIDAR_RATE_9K;
        break;

      default:
        _samp_rate = _rate.rate;
        break;
    }

    while (_samp_rate != _rate.rate) {
      ans = lidarPtr->setSamplingRate(_rate);
      success_count++;

      if (!IS_OK(ans)) {
        try_count++;

        if (try_count > 3) {
          break;
        }
      }

      if (success_count > 7) {
        break;
      }
    }

    switch (_rate.rate) {
      case YDlidarDriver::YDLIDAR_RATE_4K:
        _samp_rate = 4;
        node_counts = 720;
        each_angle = 0.5;
        break;

      case YDlidarDriver::YDLIDAR_RATE_8K:
        node_counts = 1200;
        each_angle = 0.3;
        _samp_rate = 8;
        break;

      case YDlidarDriver::YDLIDAR_RATE_9K:
        node_counts = 1200;
        each_angle = 0.3;
        _samp_rate = 9;
        break;

      default:
        break;
    }
  }

  m_SampleRate = _samp_rate;

}

/*-------------------------------------------------------------
                        checkScanFrequency
-------------------------------------------------------------*/
bool CYdLidar::checkScanFrequency() {
  float frequency = 7.4f;
  scan_frequency _scan_frequency;
  float hz = 0;
  result_t ans = RESULT_FAIL;
  m_ScanFrequency += frequencyOffset;

  if (5.0 - frequencyOffset <= m_ScanFrequency &&
      m_ScanFrequency <= 12 + frequencyOffset) {
    ans = lidarPtr->getScanFrequency(_scan_frequency) ;

    if (IS_OK(ans)) {
      frequency = _scan_frequency.frequency / 100.f;
      hz = m_ScanFrequency - frequency;

      if (hz > 0) {
        while (hz > 0.95) {
          lidarPtr->setScanFrequencyAdd(_scan_frequency);
          hz = hz - 1.0;
        }

        while (hz > 0.09) {
          lidarPtr->setScanFrequencyAddMic(_scan_frequency);
          hz = hz - 0.1;
        }

        frequency = _scan_frequency.frequency / 100.0f;
      } else {
        while (hz < -0.95) {
          lidarPtr->setScanFrequencyDis(_scan_frequency);
          hz = hz + 1.0;
        }

        while (hz < -0.09) {
          lidarPtr->setScanFrequencyDisMic(_scan_frequency);
          hz = hz + 0.1;
        }

        frequency = _scan_frequency.frequency / 100.0f;
      }
    }
  } else {
    fprintf(stderr, "current scan frequency[%f] is out of range.",
            m_ScanFrequency - frequencyOffset);
  }

  ans = lidarPtr->getScanFrequency(_scan_frequency);

  if (IS_OK(ans)) {
    frequency = _scan_frequency.frequency / 100.0f;
    m_ScanFrequency = frequency;
  }

  m_ScanFrequency -= frequencyOffset;
  node_counts = m_SampleRate * 1000 / (m_ScanFrequency - 0.1);
  each_angle = 360.0 / node_counts;
  printf("[YDLIDAR INFO] Current Scan Frequency: %fHz\n", m_ScanFrequency);
  elog(format("[YDLIDAR INFO] Current Scan Frequency: %fHz\n", m_ScanFrequency));
  return true;
}

bool CYdLidar::checkCalibrationAngle() {
  m_AngleOffset = 0.0;
  m_isAngleOffsetCorrected = false;
  bool ret = false;

  if (ydlidar::fileExists(m_CalibrationFileName)) {
    SI_Error rc = ini.LoadFile(m_CalibrationFileName.c_str());

    if (rc >= 0) {
      m_isAngleOffsetCorrected = true;
      double default_value = 179.6;
      m_AngleOffset = ini.GetDoubleValue(selectionName.c_str(),
                                         m_serial_number.c_str(),
                                         default_value);

      if (fabs(m_AngleOffset - default_value) < 0.01) {
        m_isAngleOffsetCorrected = false;
        m_AngleOffset = 0.0;
      }

      printf("[YDLIDAR INFO] Successfully obtained the %s offset angle[%f] from the calibration file[%s]\n"
             , m_isAngleOffsetCorrected ? "corrected" : "uncorrrected", m_AngleOffset,
             m_CalibrationFileName.c_str());
      elog(format("[YDLIDAR INFO] Successfully obtained the %s offset angle[%f] from the calibration file[%s]\n"
                  , m_isAngleOffsetCorrected ? "corrected" : "uncorrrected", m_AngleOffset,
                  m_CalibrationFileName.c_str()));
      ret = true;

    } else {
      printf("[YDLIDAR INFO] Failed to open calibration file[%s]\n",
             m_CalibrationFileName.c_str());
    }
  } else {
    printf("[YDLIDAR INFO] Calibration file[%s] does not exist\n",
           m_CalibrationFileName.c_str());
  }

  return ret;
}

/*-------------------------------------------------------------
                        saveRobotOffsetAngle
-------------------------------------------------------------*/
bool CYdLidar::saveOffsetAngle() {
  bool ret = true;

  if (m_StartAngleOffset) {
    ini.SetDoubleValue(selectionName.c_str(), m_serial_number.c_str(),
                       m_AngleOffset);
    SI_Error rc = ini.SaveFile(m_CalibrationFileName.c_str());

    if (rc >= 0) {
      m_StartAngleOffset = false;
      m_isAngleOffsetCorrected = true;
      printf("[YDLIDAR INFO] Current robot offset correction value[%f] is saved\n",
             m_AngleOffset);
      elog(format("[YDLIDAR INFO] Current robot offset correction value[%f] is saved\n",
                  m_AngleOffset));
    } else {
      fprintf(stderr, "Saving correction value[%f] failed\n",
              m_AngleOffset);
      elog(format("Saving correction value[%f] failed\n",
                  m_AngleOffset));
      m_isAngleOffsetCorrected = false;
      ret = false;
    }
  }


}



/*-------------------------------------------------------------
						checkCOMMs
-------------------------------------------------------------*/
bool  CYdLidar::checkCOMMs() {
  if (!lidarPtr) {
    // create the driver instance
    lidarPtr = new YDlidarDriver();

    if (!lidarPtr) {
      fprintf(stderr, "Create Driver fail\n");
      return false;
    }
  }

  if (lidarPtr->isconnected()) {
    return true;
  }

  // Is it COMX, X>4? ->  "\\.\COMX"
  if (m_SerialPort.size() >= 3) {
    if (tolower(m_SerialPort[0]) == 'c' && tolower(m_SerialPort[1]) == 'o' &&
        tolower(m_SerialPort[2]) == 'm') {
      // Need to add "\\.\"?
      if (m_SerialPort.size() > 4 || m_SerialPort[3] > '4') {
        m_SerialPort = std::string("\\\\.\\") + m_SerialPort;
      }
    }
  }

  // make connection...
  result_t op_result = lidarPtr->connect(m_SerialPort.c_str(), m_SerialBaudrate);

  if (!IS_OK(op_result)) {
    fprintf(stderr,
            "[CYdLidar] Error, cannot bind to the specified serial port[%s] and baudrate[%d]\n",
            m_SerialPort.c_str(), m_SerialBaudrate);
    return false;
  }

  return true;
}

/*-------------------------------------------------------------
                        checkStatus
-------------------------------------------------------------*/
bool CYdLidar::checkStatus() {

  if (!checkCOMMs()) {
    return false;
  }

  bool ret = getDeviceHealth();

  if (!ret) {
    delay(2000);
    ret = getDeviceHealth();

    if (!ret) {
      delay(1000);
    }
  }

  if (!getDeviceInfo()) {
    delay(2000);
    ret = getDeviceInfo();

    if (!ret) {
      return false;
    }
  }

  return true;
}

/*-------------------------------------------------------------
                        checkHardware
-------------------------------------------------------------*/
bool CYdLidar::checkHardware() {
  if (!lidarPtr) {
    return false;
  }

  if (isScanning && lidarPtr->isscanning()) {
    return true;
  }

  return false;
}

/*-------------------------------------------------------------
						initialize
-------------------------------------------------------------*/
bool CYdLidar::initialize() {
  if (!checkCOMMs()) {
    fprintf(stderr,
            "[CYdLidar::initialize] Error initializing YDLIDAR check Comms.\n");
    fflush(stderr);
    return false;
  }

  if (!checkStatus()) {
    fprintf(stderr,
            "[CYdLidar::initialize] Error initializing YDLIDAR check status.\n");
    fflush(stderr);
    return false;
  }

  return true;
}
