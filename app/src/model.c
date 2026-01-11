/**
 ******************************************************************************
 * @file    model.c
 * @author  GPM Application Team
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#include "model.h"

#include <zephyr/cache.h>
#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/mem_mgmt/mem_attr_heap.h>
#include "app_config.h"

#include "npu_cache.h"
#include "ll_aton_rt_user_api.h"
#include "od_ssd_st_pp_if.h"
#include "od_yolov8_pp_if.h"

#define DISPLAY_WIDTH DT_PROP(DT_CHOSEN(zephyr_display), width)
#define DISPLAY_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)


LOG_MODULE_REGISTER(model);

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(Default);

//YOLOV8
// static od_pp_outBuffer_t pp_detections_buffer[AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT];
// static od_pp_outBuffer_t detections[AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT];
// static int8_t scratch_buffer[AI_OD_YOLOV8_PP_TOTAL_BOXES * 6];
/* latest detections and associate mutex */
static int detections_nb = -1;
static struct k_mutex boxes_lock;

//Mobilenet
static od_pp_outBuffer_t detections[AI_OD_SSD_ST_PP_TOTAL_DETECTIONS];
static od_ssd_st_pp_static_param_t pp_params;
static int latest_inference_time = 0;

static void Run_Inference(NN_Instance_TypeDef *network_instance)
{
	LL_ATON_RT_RetValues_t ll_aton_rt_ret;

	do {
		/* Execute first/next step of Cube.AI/ATON runtime */
		ll_aton_rt_ret = LL_ATON_RT_RunEpochBlock(network_instance);
		/* Wait for next event */
		if (ll_aton_rt_ret == LL_ATON_RT_WFE)
			LL_ATON_OSAL_WFE();
	} while (ll_aton_rt_ret != LL_ATON_RT_DONE);

	LL_ATON_RT_Reset_Network(network_instance);
}

static void NPUCache_config()
{
	npu_cache_init();
	npu_cache_enable();
}

static void model_npu_init()
{
	NPUCache_config();

	LOG_INF("Npu subsystem ready");
}

static int clamp_point(int *x, int *y)
{
	int xi = *x;
	int yi = *y;

	if (*x < 0)
		*x = 0;
	if (*y < 0)
		*y = 0;
	if (*x >= NN_WIDTH)
		*x = NN_WIDTH - 1;
	if (*y >= NN_HEIGHT)
		*y = NN_HEIGHT - 1;

	return (xi != *x) || (yi != *y);
}

static void convert_length(float32_t wi, float32_t hi, int *wo, int *ho)
{
	*wo = (int) (NN_WIDTH * wi);
	*ho = (int) (NN_HEIGHT * hi);
}

static void convert_point(float32_t xi, float32_t yi, int *xo, int *yo)
{
	*xo = (int) (NN_WIDTH * xi);
	*yo = (int) (NN_HEIGHT * yi);
}

static void model_detection_to_box(od_pp_outBuffer_t *d, struct dbox *b)
{
	int xc, yc;
	int x0, y0;
	int x1, y1;
	int w, h;

	convert_point(d->x_center, d->y_center, &xc, &yc);
	convert_length(d->width, d->height, &w, &h);
	x0 = xc - (w + 1) / 2;
	y0 = yc - (h + 1) / 2;
	x1 = xc + (w + 1) / 2;
	y1 = yc + (h + 1) / 2;
	clamp_point(&x0, &y0);
	clamp_point(&x1, &y1);
	w = x1 - x0 + 1;
	h = y1 - y0 + 1;

	b->x = x0;
	b->y = y0;
	b->w = w;
	b->h = h;
}

// void model_thread_ep(void *arg1, void *arg2, void *arg3)
// {
// 	const LL_Buffer_InfoTypeDef *nn_out_info = LL_ATON_Output_Buffers_Info_Default();
// 	const LL_Buffer_InfoTypeDef *nn_in_info = LL_ATON_Input_Buffers_Info_Default();
// 	const struct device *const camera_aux_dev = arg1;
// 	// od_yolov8_pp_static_param_t pp_params;
// 	od_ssd_st_pp_static_param_t pp_params;
// 	struct video_buffer *vbuf;
// 	// od_yolov8_pp_in_centroid_t pp_input;
// 	od_ssd_st_pp_in_centroid_t pp_input;
// 	od_pp_out_t pp_output;
// 	uint32_t nn_out_len;
// 	uint32_t nn_in_len;
// 	uint8_t *nn_out;
// 	uint32_t tick;
// 	int ret;
// 	int i;

// 	ret = k_mutex_init(&boxes_lock);
// 	__ASSERT_NO_MSG(ret == 0);
// 	detections_nb = 0;

// 	model_npu_init();

// 	/* Initialize Cube.AI/ATON ... */
// 	LL_ATON_RT_RuntimeInit();
// 	/* ... and model instance */
// 	LL_ATON_RT_Init_Network(&NN_Instance_Default);

// 	// nn_in_len = LL_Buffer_len(nn_in_info);
// 	// nn_out_len = LL_Buffer_len(nn_out_info);
// 	// nn_out = LL_Buffer_addr_base(nn_out_info);
// 	// __ASSERT_NO_MSG(nn_out);

// 	/* pp init */
// 	//YOLOv8
//     // int32_t error = AI_OD_POSTPROCESS_ERROR_NO;
//     // const LL_Buffer_InfoTypeDef *buffers_info = LL_ATON_Output_Buffers_Info(&NN_Instance_Default);
//     // pp_params.raw_output_scale = *(buffers_info[0].scale);
//     // pp_params.raw_output_zero_point = *(buffers_info[0].offset);
//     // pp_params.nb_classes = AI_OD_YOLOV8_PP_NB_CLASSES;
//     // pp_params.nb_total_boxes = AI_OD_YOLOV8_PP_TOTAL_BOXES;
//     // pp_params.max_boxes_limit = AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT;
//     // pp_params.conf_threshold = AI_OD_YOLOV8_PP_CONF_THRESHOLD;
//     // pp_params.iou_threshold = AI_OD_YOLOV8_PP_IOU_THRESHOLD;
//     // pp_params.pScratchBuff = scratch_buffer;
//     // error = od_yolov8_pp_reset(&pp_params);

// 	// int32_t error = AI_OD_POSTPROCESS_ERROR_NO;
// 	// // od_ssd_st_pp_static_param_t *params = (od_ssd_st_pp_static_param_t *) params_postprocess;
// 	// pp_params.nb_classes = AI_OD_SSD_ST_PP_NB_CLASSES;
// 	// pp_params.nb_detections = AI_OD_SSD_ST_PP_TOTAL_DETECTIONS;
// 	// pp_params.max_boxes_limit = AI_OD_SSD_ST_PP_MAX_BOXES_LIMIT;
// 	// pp_params.conf_threshold = AI_OD_SSD_ST_PP_CONF_THRESHOLD;
// 	// pp_params.iou_threshold = AI_OD_SSD_ST_PP_IOU_THRESHOLD;
// 	// error = od_ssd_st_pp_reset(&pp_params);
//     // __ASSERT_NO_MSG(error);
// 	int32_t error = app_postprocess_init(&pp_params, &NN_Instance_Default);
//     __ASSERT_NO_MSG(error);

// 	while (1) {
// 		ret = video_dequeue(camera_aux_dev, &vbuf, K_FOREVER);
// 		__ASSERT_NO_MSG(ret == 0);

// 		/* run inference */
//         /* setup input buffer. No need cache ops since full hw path DCMIPP -> NPU */
// 		ret = LL_ATON_Set_User_Input_Buffer_Default(0, vbuf->buffer, nn_in_len);
// 		__ASSERT_NO_MSG(ret == LL_ATON_User_IO_NOERROR);
//         /* setup output. Invalidate ouput so post-process access latest inference result */
// 		// ret = sys_cache_data_invd_range(nn_out, nn_out_len);
// 		// __ASSERT_NO_MSG(ret == 0);
// 		 /* let's go */
// 		tick = HAL_GetTick();
// 		Run_Inference(&NN_Instance_Default);
// 		tick = HAL_GetTick() - tick;
// 		latest_inference_time = tick;

// 		ret = video_enqueue(camera_aux_dev, vbuf);
// 		__ASSERT_NO_MSG(ret == 0);

// 		/* post process */
// 		// pp_input.pRaw_detections = (int8_t *) nn_out;
// 		// pp_output.pOutBuff = pp_detections_buffer;
// 		// ret = od_yolov8_pp_process_int8(&pp_input, &pp_output, &pp_params);
// 		// pp_output.pOutBuff = detections;
// 		// float32_t **inputArray = (float32_t **)nn_out;
// 		// od_ssd_st_pp_in_centroid_t pp_input =
// 		// {
// 		// 	.pAnchors = (float32_t *) inputArray[2],
// 		// 	.pBoxes = (float32_t *) inputArray[1],
// 		// 	.pScores = (float32_t *) inputArray[0],
// 		// };
// 		// ret = od_ssd_st_pp_process(&pp_input, &pp_output, &pp_params);
// 		// __ASSERT_NO_MSG(ret == 0);
// 		float32_t *nn_out[10] = {0};
// 		pp_output.pOutBuff = detections;
// 		int32_t ret = app_postprocess_run((void **) nn_out, detections_nb, &pp_output, &pp_params);
// 		__ASSERT_NO_MSG(ret == 0);


// 		/* boxes state */
// 		ret = k_mutex_lock(&boxes_lock, K_FOREVER);
// 		__ASSERT_NO_MSG(ret == 0);

// 		detections_nb = pp_output.nb_detect;
// 		for (i = 0; i < pp_output.nb_detect; i++)
// 			detections[i] = pp_output.pOutBuff[i];

// 		ret = k_mutex_unlock(&boxes_lock);
// 		__ASSERT_NO_MSG(ret == 0);

// 		LOG_INF("Inference=%ums, detected=%d", latest_inference_time, detections_nb);
// 	}
// }

void model_thread_ep(void *arg1, void *arg2, void *arg3)
{
    const struct device *const camera_aux_dev = (const struct device *)arg1;

    const LL_Buffer_InfoTypeDef *nn_in_info  = LL_ATON_Input_Buffers_Info_Default();
    const LL_Buffer_InfoTypeDef *nn_out_info = LL_ATON_Output_Buffers_Info_Default();

    /* Input length expected by the network */
    const uint32_t nn_in_len = (uint32_t)LL_Buffer_len(nn_in_info);
    __ASSERT_NO_MSG(nn_in_len > 0);

    /* Count outputs */
    int number_output = 0;
    while (nn_out_info[number_output].name != NULL) {
        number_output++;
    }
    __ASSERT_NO_MSG(number_output > 0);

    /* Collect output pointers/lengths */
    int8_t *outs[8];
    int32_t outs_len[8];
    __ASSERT_NO_MSG(number_output <= (int)ARRAY_SIZE(outs));

    for (int i = 0; i < number_output; i++) {
        /* Use addr_start for each output descriptor; if your SDK needs addr_base, switch here */
        outs[i] = (int8_t *)LL_Buffer_addr_start(&nn_out_info[i]);
        outs_len[i] = (int32_t)LL_Buffer_len(&nn_out_info[i]);

        __ASSERT_NO_MSG(outs[i] != NULL);
        __ASSERT_NO_MSG(outs_len[i] > 0);

        LOG_INF("OUT[%d] name=%s addr=%p len=%d",
                i, nn_out_info[i].name, outs[i], outs_len[i]);
    }

    LOG_INF("NN in_len=%u, outputs=%d", nn_in_len, number_output);

    /* Init mutex / state */
    int ret = k_mutex_init(&boxes_lock);
    __ASSERT_NO_MSG(ret == 0);
    detections_nb = 0;

    /* NPU/cache init */
    model_npu_init();

    /* Runtime + network init */
    LL_ATON_RT_RuntimeInit();
    LL_ATON_RT_Init_Network(&NN_Instance_Default);

    /* Postprocess init (MUST return 0 on success) */
    int32_t pp_init = app_postprocess_init(&pp_params, &NN_Instance_Default);
    __ASSERT_NO_MSG(pp_init == 0);

    while (1) {
        struct video_buffer *vbuf = NULL;

        ret = video_dequeue(camera_aux_dev, &vbuf, K_FOREVER);
        __ASSERT_NO_MSG(ret == 0);
        __ASSERT_NO_MSG(vbuf != NULL && vbuf->buffer != NULL);

        /* If camera buffer is cacheable, flush input so NPU sees the latest bytes */
        sys_cache_data_flush_range(vbuf->buffer, nn_in_len);

        /* Bind user input */
        ret = LL_ATON_Set_User_Input_Buffer_Default(0, vbuf->buffer, nn_in_len);
        __ASSERT_NO_MSG(ret == LL_ATON_User_IO_NOERROR);

        uint32_t t0 = k_uptime_get_32();
        Run_Inference(&NN_Instance_Default);
        latest_inference_time = (int)(k_uptime_get_32() - t0);

        /* Invalidate outputs AFTER inference so CPU reads fresh NPU results */
        for (int i = 0; i < number_output; i++) {
            sys_cache_data_invd_range(outs[i], (size_t)outs_len[i]);
        }

        /* Return camera buffer ASAP */
        ret = video_enqueue(camera_aux_dev, vbuf);
        __ASSERT_NO_MSG(ret == 0);

        /* Run postprocess */
        od_pp_out_t pp_output;
        pp_output.pOutBuff = detections; /* our static detections array */

        int32_t pp_ret = app_postprocess_run(outs, number_output, &pp_output, &pp_params);
        __ASSERT_NO_MSG(pp_ret == 0);

        /* Store detections under lock */
        ret = k_mutex_lock(&boxes_lock, K_FOREVER);
        __ASSERT_NO_MSG(ret == 0);

        detections_nb = MIN((int)pp_output.nb_detect, (int)AI_OD_SSD_ST_PP_TOTAL_DETECTIONS);
        for (int i = 0; i < detections_nb; i++) {
            detections[i] = pp_output.pOutBuff[i];
        }

        ret = k_mutex_unlock(&boxes_lock);
        __ASSERT_NO_MSG(ret == 0);

		LOG_INF("Inference=%d ms, detected=%d", latest_inference_time, pp_output.nb_detect);
		LOG_INF("nn_in_len=%u (expected uint8=%u, float=%u)",
				nn_in_len,
				(uint32_t)(NN_WIDTH*NN_HEIGHT*3),
				(uint32_t)(NN_WIDTH*NN_HEIGHT*3*4));


    }
}

int model_get_boxes(struct dbox *boxes, int boxes_nb)
{
	int nb = 0;
	int ret;
	int i;

	/* not yet init */
	if (detections_nb < 0)
		return 0;

	ret = k_mutex_lock(&boxes_lock, K_FOREVER);
	__ASSERT_NO_MSG(ret == 0);

	nb = MIN(boxes_nb, detections_nb);
	for (i = 0; i < nb; i++)
		model_detection_to_box(&detections[i], &boxes[i]);

	ret = k_mutex_unlock(&boxes_lock);
	__ASSERT_NO_MSG(ret == 0);

	return nb;
}

int model_get_latest_inference_time()
{
	return latest_inference_time;
}