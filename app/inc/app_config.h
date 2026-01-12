/**
******************************************************************************
* @file    app_config.h
* @author  GPM Application Team
*
******************************************************************************
* @attention
*
* Copyright (c) 2023 STMicroelectronics.
* All rights reserved.
*
* This software is licensed under terms that can be found in the LICENSE file
* in the root directory of this software component.
* If no LICENSE file comes with this software, it is provided AS-IS.
*
******************************************************************************
*/

/* ---------------    Generated code    ----------------- */
#ifndef APP_CONFIG
#define APP_CONFIG

#include "arm_math.h"

#define USE_DCACHE

/*Defines: CMW_MIRRORFLIP_NONE; CMW_MIRRORFLIP_FLIP; CMW_MIRRORFLIP_MIRROR; CMW_MIRRORFLIP_FLIP_MIRROR;*/
#define CAMERA_FLIP CMW_MIRRORFLIP_NONE

#define ASPECT_RATIO_CROP (1) /* Crop both pipes to nn input aspect ratio; Original aspect ratio kept */
#define ASPECT_RATIO_FIT (2) /* Resize both pipe to NN input aspect ratio; Original aspect ratio not kept */
#define ASPECT_RATIO_FULLSCREEN (3) /* Resize camera image to NN input size and display a fullscreen image */
#define ASPECT_RATIO_MODE ASPECT_RATIO_CROP

/* Postprocessing type configuration */
#define POSTPROCESS_TYPE    POSTPROCESS_OD_YOLO_V8_UI

#define NN_HEIGHT     (224)
#define NN_WIDTH      (224)

#define COLOR_BGR (0)
#define COLOR_RGB (1)
#define COLOR_MODE COLOR_RGB
/* Classes */
#define NB_CLASSES   (1)
#define CLASSES_TABLE const char* classes_table[NB_CLASSES] = {\
   "person"}\

/* Postprocessing YOLO_V8 configuration */
#define AI_OD_YOLOV8_PP_NB_CLASSES        (1)
#define AI_OD_YOLOV8_PP_TOTAL_BOXES       (3549)
#define AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT   (10)
#define AI_OD_YOLOV8_PP_CONF_THRESHOLD    (0.5)
#define AI_OD_YOLOV8_PP_IOU_THRESHOLD     (0.5)

#define AI_OD_ST_YOLOX_PP_NB_ANCHORS        (1)

#define WELCOME_MSG_1         "Benchmark"
#define WELCOME_MSG_2       "Model Running in STM32 MCU internal memory"
#endif
