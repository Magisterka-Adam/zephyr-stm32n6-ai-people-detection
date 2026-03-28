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
// #include "ll_aton_rt_user_api.h"
#include "stai.h"
#include "stai_network.h"
#include "od_yolov2_pp_if.h"

#define DISPLAY_WIDTH DT_PROP(DT_CHOSEN(zephyr_display), width)
#define DISPLAY_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)

/* post process conf */
/* post process conf */
#define AI_OD_YOLOV2_PP_CONF_THRESHOLD (0.5)
#define AI_OD_YOLOV2_PP_IOU_THRESHOLD (0.5)
#define AI_OD_YOLOV2_PP_MAX_BOXES_LIMIT (10)
#define AI_OD_YOLOV2_PP_NB_CLASSES (1)
#define AI_OD_YOLOV2_PP_NB_ANCHORS (5)
#define AI_OD_YOLOV2_PP_GRID_WIDTH (7)
#define AI_OD_YOLOV2_PP_GRID_HEIGHT (7)
#define AI_OD_YOLOV2_PP_NB_INPUT_BOXES (AI_OD_YOLOV2_PP_GRID_WIDTH * AI_OD_YOLOV2_PP_GRID_HEIGHT)
static const float32_t AI_OD_YOLOV2_PP_ANCHORS[2 * AI_OD_YOLOV2_PP_NB_ANCHORS] = {
    0.9883000000f,
    3.3606000000f,
    2.1194000000f,
    5.3759000000f,
    3.0520000000f,
    9.1336000000f,
    5.5517000000f,
    9.3066000000f,
    9.7260000000f,
    11.1422000000f,
};

LOG_MODULE_REGISTER(model);

// static uint32_t nn_out_len;
// static uint32_t nn_in_len;
// static uint8_t *nn_out;
static stai_network_info info;
static stai_size output_nb;
static stai_ptr inputs[1];
static uint8_t network_ctx[STAI_NETWORK_CONTEXT_SIZE] __attribute__ ((aligned (32)));
static stai_size nn_out_len[STAI_NETWORK_OUT_NUM];
static stai_ptr outputs[STAI_NETWORK_OUT_NUM];

static stai_network_info *current_info;
static size_t output_order_index[1];

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(Default);

/* latest detections and associate mutex */
static od_pp_outBuffer_t pp_detections_buffer[AI_OD_YOLOV2_PP_MAX_BOXES_LIMIT];
static od_pp_outBuffer_t detections[AI_OD_YOLOV2_PP_MAX_BOXES_LIMIT];
static int detections_nb = -1;
static struct k_mutex boxes_lock;

static int latest_inference_time = 0;
static int number_output;
static void *outs[8]; /* adjust if you can have more than 8 outputs */
static int32_t outs_len[8];
static uint32_t nn_in_len;
static od_yolov2_pp_static_param_t pp_params;
static od_yolov2_pp_in_t pp_input;

static void Run_Inference(stai_network *network_instance)
{
	stai_return_code ret;

	do {
		ret = stai_network_run(network_instance, STAI_MODE_ASYNC);
		if (ret == STAI_RUNNING_WFE)
			LL_ATON_OSAL_WFE();
	} while (ret == STAI_RUNNING_WFE || ret == STAI_RUNNING_NO_WFE);

	ret = stai_ext_network_new_inference(network_instance);
	assert(ret == STAI_SUCCESS);
}

static void NPUCache_config()
{
	npu_cache_enable();
}

static void model_npu_init()
{
	NPUCache_config();

	LOG_INF("Npu subsystem ready");
}

static int clamp_point(int *x, int *y)
{
    int xi = *x, yi = *y;

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
    *wo = (int)(NN_WIDTH * wi);
    *ho = (int)(NN_HEIGHT * hi);
}

static void convert_point(float32_t xi, float32_t yi, int *xo, int *yo)
{
    *xo = (int)(NN_WIDTH * xi);
    *yo = (int)(NN_HEIGHT * yi);
}

static void model_detection_to_box(od_pp_outBuffer_t *d, struct dbox *b)
{
    int xc, yc;
    int x0, y0;
    int x1, y1;
    int w, h;

    convert_point(d->x_center, d->y_center, &xc, &yc);
    convert_length(d->width, d->height, &w, &h);
    x0 = xc - w / 2;
    y0 = yc - h / 2;
    x1 = x0 + w;
    y1 = y0 + h;
    clamp_point(&x0, &y0);
    clamp_point(&x1, &y1);
    w = MAX(1, x1 - x0);
    h = MAX(1, y1 - y0);

    b->x = x0;
    b->y = y0;
    b->w = w;
    b->h = h;
}

void model_thread_ep(void *arg1, void *arg2, void *arg3)
{
    // const struct device *const camera_aux_dev = arg1;

    // const LL_Buffer_InfoTypeDef *nn_in_info  = LL_ATON_Input_Buffers_Info_Default();
    // const LL_Buffer_InfoTypeDef *nn_out_info = LL_ATON_Output_Buffers_Info_Default();

    // uint32_t nn_in_len = LL_Buffer_len(nn_in_info);

    // /* ---- enumerate outputs (bare-metal style) ---- */
    // int number_output = 0;
    // while (nn_out_info[number_output].name != NULL) {
    //     number_output++;
    // }
    // __ASSERT_NO_MSG(number_output > 0);

    // void *outs[8];          /* adjust if you can have more than 8 outputs */
    // int32_t outs_len[8];

    // __ASSERT_NO_MSG(number_output <= (int)ARRAY_SIZE(outs));

    // for (int i = 0; i < number_output; i++) {
    //     outs[i]     = (void *)LL_Buffer_addr_start(&nn_out_info[i]);
    //     outs_len[i] = (int32_t)LL_Buffer_len(&nn_out_info[i]);
    //     __ASSERT_NO_MSG(outs[i] != NULL);
    // }

    // /* postprocess */
    // od_yolov8_pp_static_param_t pp_params;
    // od_pp_out_t pp_output;
    // int ret;

    // ret = k_mutex_init(&boxes_lock);
    // __ASSERT_NO_MSG(ret == 0);
    // detections_nb = 0;

    // model_npu_init();

    // LL_ATON_RT_RuntimeInit();
    // LL_ATON_RT_Init_Network(&NN_Instance_Default);

    // app_postprocess_init(&pp_params, &NN_Instance_Default); /* assumes void-return like your bare-metal */
    // /* If your app_postprocess_init returns int, change to: ret = ...; __ASSERT_NO_MSG(ret == 0); */

    // LOG_INF("NN in_len=%u, outputs=%d", nn_in_len, number_output);

    // while (1) {
    //     struct video_buffer *vbuf = NULL;

    //     /* If you get no logs, you may be stuck here waiting for frames */
    //     ret = video_dequeue(camera_aux_dev, &vbuf, K_FOREVER);
    //     __ASSERT_NO_MSG(ret == 0);
    //     __ASSERT_NO_MSG(vbuf && vbuf->buffer);

    //     uint8_t *p = vbuf->buffer;
    //     for (size_t i = 0; i < nn_in_len; i += 3) {
    //         uint8_t r = p[i+0];
    //         p[i+0] = p[i+2];
    //         p[i+2] = r;
    //     }
    //     sys_cache_data_flush_range(vbuf->buffer, nn_in_len); /* ensure swap is visible */

    //     /* set input buffer to camera frame */
    //     ret = LL_ATON_Set_User_Input_Buffer_Default(0, vbuf->buffer, nn_in_len);
    //     __ASSERT_NO_MSG(ret == LL_ATON_User_IO_NOERROR);

    //     /* invalidate all outputs before postprocess reads them */
    //     for (int i = 0; i < number_output; i++) {
    //         sys_cache_data_invd_range(outs[i], outs_len[i]);
    //     }

    //     uint32_t t0 = k_uptime_get_32();
    //     Run_Inference(&NN_Instance_Default);
    //     uint32_t dt = k_uptime_get_32() - t0;
    //     latest_inference_time = (int)dt;

    //     /* NOW invalidate outputs so CPU reads the fresh NPU result */
    //     // sys_cache_data_invd_range(nn_out, nn_in_len);

    //     ret = video_enqueue(camera_aux_dev, vbuf);
    //     __ASSERT_NO_MSG(ret == 0);

    //     int32_t pp_ret = app_postprocess_run(outs, number_output, &pp_output, &pp_params);
    //     __ASSERT_NO_MSG(pp_ret == 0);

    //     /* store detections */
    //     ret = k_mutex_lock(&boxes_lock, K_FOREVER);
    //     __ASSERT_NO_MSG(ret == 0);

    //     detections_nb = MIN((int)pp_output.nb_detect, AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT);
    //     for (int i = 0; i < detections_nb; i++) {
    //         detections[i] = pp_output.pOutBuff[i];
    //     }

    //     ret = k_mutex_unlock(&boxes_lock);
    //     __ASSERT_NO_MSG(ret == 0);

    //     LOG_INF("Inference=%ums, detected=%d", latest_inference_time, detections_nb);
    // }
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

void nn_init()
{
    // const LL_Buffer_InfoTypeDef *nn_in_info = LL_ATON_Input_Buffers_Info_Default();
    // const LL_Buffer_InfoTypeDef *nn_out_info = LL_ATON_Output_Buffers_Info_Default();

    int ret = k_mutex_init(&boxes_lock);
    __ASSERT_NO_MSG(ret == 0);
    detections_nb = 0;

    model_npu_init();

    /* initialize runtime */
    ret = stai_runtime_init();
    __ASSERT_NO_MSG(ret == STAI_SUCCESS);
    /* init model instance */
    ret = stai_network_init(network_ctx);
    __ASSERT_NO_MSG(ret == STAI_SUCCESS);

    /* setup buffers size */
    ret = stai_network_get_info(network_ctx, &info);
    __ASSERT_NO_MSG(ret == STAI_SUCCESS);
    __ASSERT_NO_MSG(info.n_inputs == 1);
    __ASSERT_NO_MSG(info.n_outputs == STAI_NETWORK_OUT_NUM);
    ret = stai_network_get_outputs(network_ctx, outputs, &output_nb);
    __ASSERT_NO_MSG(ret == STAI_SUCCESS);
    
    /* Get input buffer size */
    nn_in_len = info.inputs[0].size_bytes;
    
    /* Setup output buffers and metadata (YOLOv2 has a single output) */
    number_output = (int)output_nb;
    for (int i = 0; i < number_output; i++) {
        nn_out_len[i] = info.outputs[i].size_bytes;
        outs[i] = outputs[i];
        outs_len[i] = (int32_t)info.outputs[i].size_bytes;
    }
    output_order_index[0] = 0;

    /* pp init - setup postprocessor parameters */
    pp_params.conf_threshold = AI_OD_YOLOV2_PP_CONF_THRESHOLD;
    pp_params.iou_threshold = AI_OD_YOLOV2_PP_IOU_THRESHOLD;
    pp_params.nb_anchors = AI_OD_YOLOV2_PP_NB_ANCHORS;
    pp_params.nb_classes = AI_OD_YOLOV2_PP_NB_CLASSES;
    pp_params.grid_height = AI_OD_YOLOV2_PP_GRID_HEIGHT;
    pp_params.grid_width = AI_OD_YOLOV2_PP_GRID_WIDTH;
    pp_params.nb_input_boxes = AI_OD_YOLOV2_PP_NB_INPUT_BOXES;
    pp_params.pAnchors = AI_OD_YOLOV2_PP_ANCHORS;
    pp_params.max_boxes_limit = AI_OD_YOLOV2_PP_MAX_BOXES_LIMIT;
    
    /* Output tensor is STAI_FORMAT_S8, so int8 postprocess needs quant params */
    pp_params.raw_scale = (info.outputs[0].scale.size > 0) ? info.outputs[0].scale.data[0] : 1.0f;
    pp_params.raw_zero_point = (info.outputs[0].zeropoint.size > 0) ? (int8_t)info.outputs[0].zeropoint.data[0] : 0;
    
    /* Allocate scratch buffer for postprocessor */
    static uint8_t scratch_buffer[AI_OD_YOLOV2_PP_GRID_WIDTH * AI_OD_YOLOV2_PP_GRID_HEIGHT *
                                  AI_OD_YOLOV2_PP_NB_ANCHORS * sizeof(od_pp_outBuffer_t)]
        __attribute__((aligned(32)));
    pp_params.pScratchBuffer = scratch_buffer;
    
    ret = od_yolov2_pp_reset(&pp_params);
    __ASSERT_NO_MSG(ret == 0);
        LOG_INF("NN in_len=%u, outputs=%d, out0_format=0x%08x, scale=%.6f zp=%d", nn_in_len,
            number_output, (uint32_t)info.outputs[0].format, pp_params.raw_scale,
            pp_params.raw_zero_point);
}

void run_nn_from_sd_card(uint8_t *image)
{
    od_pp_out_t pp_output;
    int ret;

    /* Set input buffer */
    inputs[0] = image;
    ret = stai_network_set_inputs(network_ctx, inputs, ARRAY_SIZE(inputs));
    __ASSERT_NO_MSG(ret == STAI_SUCCESS);

    /* Flush input buffer from CPU cache to DRAM so the NPU can read it */
    sys_cache_data_flush_range(image, nn_in_len);
    
    /* Invalidate all output buffers before inference so we read fresh results */
    for (int i = 0; i < output_nb; i++) {
        ret = sys_cache_data_invd_range(outputs[i], nn_out_len[i]);
        __ASSERT_NO_MSG(ret == 0);
    }

    /* Run inference */
    uint32_t t0 = k_uptime_get_32();
    Run_Inference(&network_ctx);
    uint32_t dt = k_uptime_get_32() - t0;
    latest_inference_time = (int)dt;

    /* NOW invalidate outputs so CPU reads the fresh NPU result */

    for (int i = 0; i < number_output; i++)
    {
        sys_cache_data_invd_range(outs[i], outs_len[i]);
    }

    pp_input.pRaw_detections = (float32_t *)outputs;
    pp_output.pOutBuff = pp_detections_buffer;
    ret = od_yolov2_pp_process(&pp_input, &pp_output, &pp_params);
    if (ret != 0) {
        LOG_ERR("Postprocessor error: %d", ret);
    }
    __ASSERT_NO_MSG(ret == 0);

    /* Store detections with mutex protection */
    ret = k_mutex_lock(&boxes_lock, K_FOREVER);
    __ASSERT_NO_MSG(ret == 0);

    detections_nb = MIN((int)pp_output.nb_detect, AI_OD_YOLOV2_PP_MAX_BOXES_LIMIT);
    for (int i = 0; i < detections_nb; i++) {
        detections[i] = pp_output.pOutBuff[i];
    }

    ret = k_mutex_unlock(&boxes_lock);
    __ASSERT_NO_MSG(ret == 0);

    LOG_INF("Inference=%ums, detected=%d", latest_inference_time, detections_nb);
}