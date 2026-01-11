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
#define POSTPROCESS_TYPE    POSTPROCESS_OD_ST_SSD_UF

#define NN_HEIGHT     (256)
#define NN_WIDTH      (256)
#define NN_BPP 3

#define COLOR_BGR (0)
#define COLOR_RGB (1)
#define COLOR_MODE COLOR_RGB
/* Classes */
#define NB_CLASSES        (2)
#define CLASSES_TABLE const char* classes_table[NB_CLASSES] = {\
"background",   "person"}\
/* Postprocessing ST_SSD configuration */
#define AI_OD_SSD_ST_PP_NB_CLASSES         (2)
#define AI_OD_SSD_ST_PP_IOU_THRESHOLD      (0.2)
#define AI_OD_SSD_ST_PP_CONF_THRESHOLD     (0.2)
#define AI_OD_SSD_ST_PP_MAX_BOXES_LIMIT    (10)
#define AI_OD_SSD_ST_PP_TOTAL_DETECTIONS   (6825)
#define WELCOME_MSG_1         "st_ssd_mobilenet_v1_025_256_int8.tflite"
#define WELCOME_MSG_2       "Model Running in STM32 MCU internal memory"

#endif      /* APP_CONFIG */
