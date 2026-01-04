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
#include "od_yolov8_pp_if.h"
#include "app_postprocess.h"

#define DISPLAY_WIDTH DT_PROP(DT_CHOSEN(zephyr_display), width)
#define DISPLAY_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)

/* post process conf */
#define AI_OD_YOLOV8_PP_NB_CLASSES        (1)
// #define AI_OD_YOLOV8_PP_TOTAL_BOXES       (3549)
#define AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT   (10)
#define AI_OD_YOLOV8_PP_CONF_THRESHOLD    (0.5)
#define AI_OD_YOLOV8_PP_IOU_THRESHOLD     (0.5)

#define AI_OD_ST_YOLOX_PP_NB_ANCHORS        (1)


LOG_MODULE_REGISTER(model);

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(Default);

/* latest detections and associate mutex */
static od_pp_outBuffer_t pp_detections_buffer[AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT];
static od_pp_outBuffer_t detections[AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT];
static int detections_nb = -1;
static struct k_mutex boxes_lock;

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
	if (*x >= DISPLAY_WIDTH)
		*x = DISPLAY_WIDTH - 1;
	if (*y >= DISPLAY_HEIGHT)
		*y = DISPLAY_HEIGHT - 1;

	return (xi != *x) || (yi != *y);
}

static void convert_length(float32_t wi, float32_t hi, int *wo, int *ho)
{
	*wo = (int) (DISPLAY_WIDTH * wi);
	*ho = (int) (DISPLAY_HEIGHT * hi);
}

static void convert_point(float32_t xi, float32_t yi, int *xo, int *yo)
{
	*xo = (int) (DISPLAY_WIDTH * xi);
	*yo = (int) (DISPLAY_HEIGHT * yi);
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

void model_thread_ep(void *arg1, void *arg2, void *arg3)
{
    const struct device *const camera_aux_dev = arg1;

    const LL_Buffer_InfoTypeDef *nn_in_info  = LL_ATON_Input_Buffers_Info_Default();
    const LL_Buffer_InfoTypeDef *nn_out_info = LL_ATON_Output_Buffers_Info_Default();

    uint32_t nn_in_len = LL_Buffer_len(nn_in_info);

    /* ---- enumerate outputs (bare-metal style) ---- */
    int number_output = 0;
    while (nn_out_info[number_output].name != NULL) {
        number_output++;
    }
    __ASSERT_NO_MSG(number_output > 0);

    void *outs[8];          /* adjust if you can have more than 8 outputs */
    int32_t outs_len[8];

    __ASSERT_NO_MSG(number_output <= (int)ARRAY_SIZE(outs));

    for (int i = 0; i < number_output; i++) {
        outs[i]     = (void *)LL_Buffer_addr_start(&nn_out_info[i]);
        outs_len[i] = (int32_t)LL_Buffer_len(&nn_out_info[i]);
        __ASSERT_NO_MSG(outs[i] != NULL);
    }

    /* postprocess */
    od_yolov8_pp_static_param_t pp_params;
    od_pp_out_t pp_output;
    int ret;

    ret = k_mutex_init(&boxes_lock);
    __ASSERT_NO_MSG(ret == 0);
    detections_nb = 0;

    model_npu_init();

    LL_ATON_RT_RuntimeInit();
    LL_ATON_RT_Init_Network(&NN_Instance_Default);

    app_postprocess_init(&pp_params, &NN_Instance_Default); /* assumes void-return like your bare-metal */
    /* If your app_postprocess_init returns int, change to: ret = ...; __ASSERT_NO_MSG(ret == 0); */

    LOG_INF("NN in_len=%u, outputs=%d", nn_in_len, number_output);

    while (1) {
        struct video_buffer *vbuf = NULL;

        /* If you get no logs, you may be stuck here waiting for frames */
        ret = video_dequeue(camera_aux_dev, &vbuf, K_FOREVER);
        __ASSERT_NO_MSG(ret == 0);
        __ASSERT_NO_MSG(vbuf && vbuf->buffer);

        uint8_t *p = vbuf->buffer;
        for (size_t i = 0; i < nn_in_len; i += 3) {
            uint8_t r = p[i+0];
            p[i+0] = p[i+2];
            p[i+2] = r;
        }
        sys_cache_data_flush_range(vbuf->buffer, nn_in_len); /* ensure swap is visible */


        /* set input buffer to camera frame */
        ret = LL_ATON_Set_User_Input_Buffer_Default(0, vbuf->buffer, nn_in_len);
        __ASSERT_NO_MSG(ret == LL_ATON_User_IO_NOERROR);

        /* invalidate all outputs before postprocess reads them */
        for (int i = 0; i < number_output; i++) {
            sys_cache_data_invd_range(outs[i], outs_len[i]);
        }

        uint32_t t0 = k_uptime_get_32();
        Run_Inference(&NN_Instance_Default);
        uint32_t dt = k_uptime_get_32() - t0;
        latest_inference_time = (int)dt;
        
        
        /* NOW invalidate outputs so CPU reads the fresh NPU result */
        // sys_cache_data_invd_range(nn_out, nn_in_len);
        
        ret = video_enqueue(camera_aux_dev, vbuf);
        __ASSERT_NO_MSG(ret == 0);

        int32_t pp_ret = app_postprocess_run(outs, number_output, &pp_output, &pp_params);
        __ASSERT_NO_MSG(pp_ret == 0);

        /* store detections */
        ret = k_mutex_lock(&boxes_lock, K_FOREVER);
        __ASSERT_NO_MSG(ret == 0);

        detections_nb = MIN((int)pp_output.nb_detect, AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT);
        for (int i = 0; i < detections_nb; i++) {
            detections[i] = pp_output.pOutBuff[i];
        }

        ret = k_mutex_unlock(&boxes_lock);
        __ASSERT_NO_MSG(ret == 0);

        LOG_INF("Inference=%ums, detected=%d", latest_inference_time, detections_nb);
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
