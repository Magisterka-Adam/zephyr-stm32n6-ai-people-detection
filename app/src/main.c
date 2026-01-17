/**
 ******************************************************************************
 * @file    main.c
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

#include <zephyr/kernel.h>
#include <zephyr/debug/cpu_load.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <lvgl.h>
#include <font/lv_font.h>

#include <stdarg.h>

#include "model.h"

#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <zephyr/data/json.h>

#include <zephyr/fs/fs.h>
#include <ff.h>

LOG_MODULE_REGISTER(main);

#define DISPLAY_WIDTH DT_PROP(DT_CHOSEN(zephyr_display), width)
#define DISPLAY_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)
#define DISPLAY_BPP 2

#define NN_WIDTH CONFIG_NPU_WIDTH
#define NN_HEIGHT CONFIG_NPU_HEIGHT
#define NN_BPP 3
#define NPU_BUFFER_POOL_ALIGN 32

static struct k_thread nn_thread;
static K_THREAD_STACK_DEFINE(nn_thread_stack, 4096);

#if defined(CONFIG_DISK_DRIVER_MMC)
#define DISK_DRIVE_NAME "SD2"
#else
#define DISK_DRIVE_NAME "SD"
#endif

#define DISK_MOUNT_PT "/" DISK_DRIVE_NAME ":"

static FATFS fat_fs;

static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.storage_dev = (void *)DISK_DRIVE_NAME,
	.mnt_point = DISK_MOUNT_PT,
};

#define MAX_GT_BOXES 64

struct gt_boxes
{
	int count;
	float xywh[MAX_GT_BOXES][4];
};

struct box_xyxy
{
	float x0, y0, x1, y1;
};

static int img_with_gt = 0;
static int img_with_pred = 0;
static int gt_matched = 0;
static int pred_matched = 0;

static inline struct box_xyxy xywh_to_xyxy(float x, float y, float w, float h)
{
	struct box_xyxy b = {x, y, x + w, y + h};
	return b;
}

static inline struct box_xyxy dbox_to_xyxy(const struct dbox *b)
{
	struct box_xyxy o = {b->x, b->y, b->x + b->w, b->y + b->h};
	return o;
}

static float iou_xyxy(struct box_xyxy a, struct box_xyxy b)
{
	float x1 = MAX(a.x0, b.x0);
	float y1 = MAX(a.y0, b.y0);
	float x2 = MIN(a.x1, b.x1);
	float y2 = MIN(a.y1, b.y1);

	float w = x2 - x1;
	float h = y2 - y1;
	if (w <= 0.f || h <= 0.f)
		return 0.f;

	float inter = w * h;
	float area_a = MAX(0.f, a.x1 - a.x0) * MAX(0.f, a.y1 - a.y0);
	float area_b = MAX(0.f, b.x1 - b.x0) * MAX(0.f, b.y1 - b.y0);
	if (area_a <= 0.f || area_b <= 0.f)
		return 0.f;

	return inter / (area_a + area_b - inter + 1e-6f);
}
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static void rstrip(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' ||
				 s[n - 1] == ' ' || s[n - 1] == '\t'))
	{
		s[--n] = 0;
	}
}

static void lskip(char **ps)
{
	while (**ps == ' ' || **ps == '\t')
	{
		(*ps)++;
	}
}

static int gt_find_person_boxes_csv(const char *csv_path,
									const char *filename,
									struct gt_boxes *out)
{
	struct fs_file_t file;
	fs_file_t_init(&file);

	/* Always initialize output */
	memset(out, 0, sizeof(*out));

	int ret = fs_open(&file, csv_path, FS_O_READ);
	if (ret < 0)
	{
		LOG_ERR("fs_open(%s) failed: %d", csv_path, ret);
		return ret;
	}

	/* Larger buffer to avoid truncating long lines */
	static char line[4096];
	size_t pos = 0;
	bool found = false;

	for (;;)
	{
		uint8_t ch;
		ssize_t r = fs_read(&file, &ch, 1);
		if (r <= 0)
		{
			break; /* EOF */
		}

		if (ch == '\n' || pos == sizeof(line) - 1)
		{
			line[pos] = 0;
			pos = 0;

			/* If we hit buffer limit without '\n', the line is truncated.
			 * We'll still parse what we have, but it's likely incomplete.
			 */
			rstrip(line);

			if (line[0] == 0)
			{
				continue;
			}

			/* line: fname,count,x,y,w,h,x,y,w,h,... */
			char *save = NULL;
			char *tok = strtok_r(line, ",", &save);
			if (!tok)
			{
				continue;
			}

			lskip(&tok);
			rstrip(tok);

			if (strcmp(tok, filename) != 0)
			{
				continue;
			}

			/* Matched filename */
			found = true;

			tok = strtok_r(NULL, ",", &save);
			if (!tok)
			{
				fs_close(&file);
				return -EINVAL;
			}
			lskip(&tok);
			rstrip(tok);

			int n = atoi(tok);
			if (n < 0)
				n = 0;
			if (n > MAX_GT_BOXES)
				n = MAX_GT_BOXES;

			for (int i = 0; i < n; i++)
			{
				for (int k = 0; k < 4; k++)
				{
					tok = strtok_r(NULL, ",", &save);
					if (!tok)
					{
						fs_close(&file);
						return -EINVAL;
					}
					lskip(&tok);
					rstrip(tok);
					out->xywh[i][k] = strtof(tok, NULL);
				}
			}

			out->count = n;
			break; /* done */
		}
		else
		{
			line[pos++] = (char)ch;
		}
	}

	fs_close(&file);

	if (!found)
	{
		return -ENOENT; /* not found */
	}
	return 0;
}

uint8_t image_data[NN_HEIGHT * NN_WIDTH * NN_BPP];
static struct video_buffer *sd_vbuf;

void nn_init();
void run_nn_from_sd_card(uint8_t *image);

static int display_setup(const struct device *const display_dev)
{
	int ret;

	ret = display_set_pixel_format(display_dev, PIXEL_FORMAT_RGB_565);
	__ASSERT_NO_MSG(ret == 0);

	ret = display_blanking_off(display_dev);
	__ASSERT_NO_MSG(ret == 0);

	return 0;
}

static void print_text(lv_layer_t *layer, int x, int y, lv_text_align_t align, char *fmt, ...)
{
	lv_draw_label_dsc_t tdsc;
	char text_buffer[128];
	lv_area_t coords;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(text_buffer, sizeof(text_buffer), fmt, ap);

	/* shadow */
	lv_draw_label_dsc_init(&tdsc);
	tdsc.color = lv_color_make(16, 16, 16);
	tdsc.font = &lv_font_unscii_16;
	coords.x1 = x + 1;
	coords.y1 = y + 1;
	coords.x2 = DISPLAY_WIDTH;
	coords.y2 = DISPLAY_HEIGHT;
	tdsc.text = text_buffer;
	tdsc.text_local = 1;
	tdsc.align = align;
	lv_draw_label(layer, &tdsc, &coords);

	/* text */
	tdsc.color = lv_color_make(255, 255, 255);
	coords.x1 = x;
	coords.y1 = y;
	lv_draw_label(layer, &tdsc, &coords);
	va_end(ap);
}

static void decorate_canvas(lv_obj_t *canvas)
{
	int latest_inference_time = model_get_latest_inference_time();
	const lv_font_t *font = &lv_font_unscii_16;
	lv_draw_rect_dsc_t rdsc;
	struct dbox boxes[10];
	lv_area_t coords;
	lv_layer_t layer;
	struct dbox *b;
	uint32_t ver;
	int load;
	int nb;
	int i;

	ver = sys_kernel_version_get();
	nb = model_get_boxes(boxes, ARRAY_SIZE(boxes));

	lv_canvas_init_layer(canvas, &layer);
	/* Draw banner */
	print_text(&layer, 0, 0, LV_TEXT_ALIGN_CENTER, "People Detection demo");
	print_text(&layer, 0, font->line_height, LV_TEXT_ALIGN_CENTER, "Powered by zephyr OS %d.%d.%d",
			   SYS_KERNEL_VER_MAJOR(ver), SYS_KERNEL_VER_MINOR(ver), SYS_KERNEL_VER_PATCHLEVEL(ver));

	/* Draw inference time */
	print_text(&layer, 0, DISPLAY_HEIGHT - font->line_height * 3, LV_TEXT_ALIGN_LEFT, "Inference %d ms",
			   latest_inference_time);
	latest_inference_time = latest_inference_time ? latest_inference_time : 1;
	print_text(&layer, 0, DISPLAY_HEIGHT - font->line_height * 2, LV_TEXT_ALIGN_LEFT, "Fps       %d",
			   1000 / latest_inference_time);
#ifdef CONFIG_CPU_LOAD
	load = cpu_load_get(1);
	print_text(&layer, 0, DISPLAY_HEIGHT - font->line_height * 1, LV_TEXT_ALIGN_LEFT, "cpu load  %d.%d%%",
			   load / 10, load % 10);
#else
	(void)load;
#endif

	/* Draw boxes */
	lv_draw_rect_dsc_init(&rdsc);
	rdsc.border_color = lv_palette_main(LV_PALETTE_GREEN);
	rdsc.border_width = 2;
	rdsc.radius = 10;
	rdsc.bg_opa = 0;
	for (i = 0; i < nb; i++)
	{
		b = &boxes[i];
		coords.x1 = b->x;
		coords.y1 = b->y;
		coords.x2 = b->x + b->w;
		coords.y2 = b->y + b->h;
		lv_draw_rect(&layer, &rdsc, &coords);
	}

	lv_canvas_finish_layer(canvas, &layer);
}

static int video_crop_setup(const struct device *const video_dev, uint32_t sensor_width, uint32_t sensor_height)
{
	const float ratioy = (float)sensor_height / DISPLAY_HEIGHT;
	const float ratiox = (float)sensor_width / DISPLAY_WIDTH;
	const float ratio = MIN(ratiox, ratioy);
	struct video_selection crop = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	int ret;

	__ASSERT_NO_MSG(ratio >= 1);
	__ASSERT_NO_MSG(ratio < 64);

	crop.target = VIDEO_SEL_TGT_CROP;
	crop.rect.width = DISPLAY_WIDTH * ratio;
	crop.rect.height = DISPLAY_HEIGHT * ratio;
	crop.rect.left = (sensor_width - crop.rect.width + 1) / 2;
	crop.rect.top = (sensor_height - crop.rect.height + 1) / 2;

	ret = video_set_selection(video_dev, &crop);
	__ASSERT_NO_MSG(ret == 0);

	return 0;
}

static int video_resize_setup(const struct device *const video_dev, uint32_t width, uint32_t height)
{
	struct video_selection comp = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	int ret;

	comp.target = VIDEO_SEL_TGT_COMPOSE;
	comp.rect.left = 0;
	comp.rect.top = 0;
	comp.rect.width = width;
	comp.rect.height = height;

	ret = video_set_selection(video_dev, &comp);
	__ASSERT_NO_MSG(ret == 0);

	return 0;
}

static int video_alloc_and_queue_buffers(const struct device *const video_dev, size_t bsize, int align)
{
	struct video_buffer *buffers[2];
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(buffers); i++)
	{
		buffers[i] = video_buffer_aligned_alloc(bsize, align, K_FOREVER);
		__ASSERT_NO_MSG(buffers[i]);
		buffers[i]->type = VIDEO_BUF_TYPE_OUTPUT;
		ret = video_enqueue(video_dev, buffers[i]);
		__ASSERT_NO_MSG(ret == 0);
		LOG_INF("buffer %p(%p) queue to %s pipe", (void *)buffers[i], (void *)buffers[i]->buffer, video_dev->name);
	}

	return 0;
}

static int video_main_setup(const struct device *const video_dev, uint32_t sensor_width, uint32_t sensor_height)
{
	struct video_format fmt;
	size_t bsize;
	int ret;

	/* setup crop, compose and output */
	ret = video_crop_setup(video_dev, sensor_width, sensor_height);
	__ASSERT_NO_MSG(ret == 0);
	ret = video_resize_setup(video_dev, DISPLAY_WIDTH, DISPLAY_HEIGHT);
	__ASSERT_NO_MSG(ret == 0);
	fmt.pixelformat = VIDEO_FOURCC_FROM_STR("RGBP");
	fmt.width = DISPLAY_WIDTH;
	fmt.height = DISPLAY_HEIGHT;
	fmt.pitch = fmt.width * DISPLAY_BPP;
	ret = video_set_format(video_dev, &fmt);
	__ASSERT_NO_MSG(ret == 0);

	/* alloc buffers and queue them */
	bsize = fmt.pitch * fmt.height;
	ret = video_alloc_and_queue_buffers(video_dev, bsize, CONFIG_VIDEO_BUFFER_POOL_ALIGN);
	__ASSERT_NO_MSG(ret == 0);

	return 0;
}

static int video_aux_setup(const struct device *const video_dev, uint32_t sensor_width, uint32_t sensor_height)
{
	struct video_format fmt;
	size_t bsize;
	int ret;

	/* setup crop, compose and output */
	ret = video_crop_setup(video_dev, sensor_width, sensor_height);
	__ASSERT_NO_MSG(ret == 0);
	ret = video_resize_setup(video_dev, NN_WIDTH, NN_HEIGHT);
	__ASSERT_NO_MSG(ret == 0);
	fmt.pixelformat = VIDEO_FOURCC_FROM_STR("RGB3");
	fmt.width = NN_WIDTH;
	fmt.height = NN_HEIGHT;
	fmt.pitch = fmt.width * NN_BPP;
	ret = video_set_format(video_dev, &fmt);
	__ASSERT_NO_MSG(ret == 0);

	/* alloc buffers and queue them */
	bsize = fmt.pitch * fmt.height;
	ret = video_alloc_and_queue_buffers(video_dev, bsize, NPU_BUFFER_POOL_ALIGN);
	__ASSERT_NO_MSG(ret == 0);

	return 0;
}

static int video_setup(const struct device *const main_dev, const struct device *const aux_dev)
{
	struct video_format fmt;
	uint32_t sensor_height;
	uint32_t sensor_width;
	int ret;

	/* get default format to get sensor width / height */
	ret = video_get_format(main_dev, &fmt);
	__ASSERT_NO_MSG(ret == 0);
	sensor_height = fmt.height;
	sensor_width = fmt.width;

	ret = video_main_setup(main_dev, sensor_width, sensor_height);
	__ASSERT_NO_MSG(ret == 0);

	ret = video_aux_setup(aux_dev, sensor_width, sensor_height);
	__ASSERT_NO_MSG(ret == 0);

	return 0;
}
// /* --- Simple RGB565 framebuffer for the LVGL canvas --- */
// static uint16_t jpg_fb[DISPLAY_WIDTH * DISPLAY_HEIGHT];

static int ls_dir(const char *path)
{
	int res;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;
	int count = 0;

	fs_dir_t_init(&dirp);

	/* Verify fs_opendir() */
	res = fs_opendir(&dirp, path);
	if (res)
	{
		LOG_INF("Error opening dir %s [%d]", path, res);
		return res;
	}

	LOG_INF("\nListing dir %s ...", path);
	for (;;)
	{
		/* Verify fs_readdir() */
		res = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (res || entry.name[0] == 0)
		{
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR)
		{
			LOG_INF("[DIR ] %s", entry.name);
		}
		else
		{
			LOG_INF("[FILE] %s (size = %zu)",
					entry.name, entry.size);
			ssize_t bytes;
			struct fs_file_t file;
			int ret;

			fs_file_t_init(&file);

			char full_path[256];
			snprintf(full_path, sizeof(full_path), "%s/%s", path, entry.name);

			ret = fs_open(&file, full_path, FS_O_READ);
			if (ret < 0)
			{
				LOG_ERR("fs_open(%s) failed (%d)", full_path, ret);
				return ret;
			}

			bytes = fs_read(&file, sd_vbuf->buffer, NN_HEIGHT * NN_WIDTH * NN_BPP);
			__ASSERT_NO_MSG(bytes == NN_HEIGHT * NN_WIDTH * NN_BPP);

			run_nn_from_sd_card(sd_vbuf->buffer);

			fs_close(&file);
			struct gt_boxes gt = {0};
			int gr = gt_find_person_boxes_csv("/SD:/benchmark_224.csv", entry.name, &gt);
			if (gr < 0)
			{
				LOG_WRN("GT lookup error %d for %s", gr, entry.name);
				continue;
			}

			struct dbox preds[10];
			int n_pred = model_get_boxes(preds, ARRAY_SIZE(preds));

			LOG_INF("Detection for annotations: %d", gt.count);
			if (gt.count > 0)
			{
				LOG_INF("GT0: x=%.1f y=%.1f w=%.1f h=%.1f", gt.xywh[0][0], gt.xywh[0][1], gt.xywh[0][2], gt.xywh[0][3]);
			}
			if (n_pred > 0)
			{
				LOG_INF("P0 : x=%d y=%d w=%d h=%d", preds[0].x, preds[0].y, preds[0].w, preds[0].h);
			}

			bool has_pred = (n_pred > 0);
			bool has_gt = (gt.count > 0);

			if (has_gt)
				img_with_gt++;
			if (has_pred)
				img_with_pred++;

			bool matched = false;
			if (has_gt && has_pred)
			{
				for (int gi = 0; gi < gt.count && !matched; gi++)
				{
					struct box_xyxy g = xywh_to_xyxy(gt.xywh[gi][0], gt.xywh[gi][1],
													 gt.xywh[gi][2], gt.xywh[gi][3]);
					for (int pi = 0; pi < n_pred; pi++)
					{
						struct box_xyxy p = dbox_to_xyxy(&preds[pi]);
						if (iou_xyxy(g, p) >= 0.5f)
						{ // IOU_THRESH
							matched = true;
							break;
						}
					}
				}
			}

			if (matched)
			{
				gt_matched++;
				pred_matched++;
			}
		}
		count++;
		if (count > 1500)
		{
			LOG_INF("END OF BENCHMARK");
			break;
		}
	}

	/* Verify fs_closedir() */
	fs_closedir(&dirp);
	if (res == 0)
	{
		res = count;
	}

	LOG_INF("Images with GT person   : %d", img_with_gt);
	LOG_INF("Images with predictions : %d", img_with_pred);
	LOG_INF("GT matched images       : %d", gt_matched);
	LOG_INF("Pred matched images     : %d", pred_matched);

	if (img_with_gt)
	{
		LOG_INF("Recall  : %d/%d = %d.%02d",
				gt_matched, img_with_gt,
				(gt_matched * 100) / img_with_gt,
				((gt_matched * 10000) / img_with_gt) % 100);
	}
	if (img_with_pred)
	{
		LOG_INF("Prec    : %d/%d = %d.%02d",
				pred_matched, img_with_pred,
				(pred_matched * 100) / img_with_pred,
				((pred_matched * 10000) / img_with_pred) % 100);
	}

	return res;
}

int main()
{
	// const struct device *const camera_aux_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera_aux));
	const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	// const struct device *const video_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
	// struct video_buffer *vbuf;
	// lv_obj_t *canvas;
	k_tid_t nn_tid;
	int ret;

	// __ASSERT_NO_MSG(device_is_ready(video_dev));
	// __ASSERT_NO_MSG(device_is_ready(camera_aux_dev));
	__ASSERT_NO_MSG(device_is_ready(display_dev));

	// /* move main thread priority to lowest one so we let others thread a chance to run */
	// k_thread_priority_set(k_current_get(), K_LOWEST_APPLICATION_THREAD_PRIO);

	// /* create thread for nn process */
	// nn_tid = k_thread_create(&nn_thread, nn_thread_stack, K_THREAD_STACK_SIZEOF(nn_thread_stack), model_thread_ep,
	// 			 (void *) NULL, NULL, NULL, 0, 0, K_NO_WAIT);
	// __ASSERT_NO_MSG(nn_tid);

	// /* Configure display */
	ret = display_setup(display_dev);
	__ASSERT_NO_MSG(ret == 0);

	nn_init();
	sd_vbuf = video_buffer_aligned_alloc(NN_HEIGHT * NN_WIDTH * NN_BPP, 32, K_FOREVER);
	__ASSERT_NO_MSG(sd_vbuf && sd_vbuf->buffer);

	// /* Configure video pipe */
	// ret = video_setup(video_dev, camera_aux_dev);
	// __ASSERT_NO_MSG(ret == 0);

	// /* Start main pipe */
	// LOG_INF("Starting main pipe");
	// ret = video_stream_start(video_dev, VIDEO_BUF_TYPE_OUTPUT);
	// __ASSERT_NO_MSG(ret == 0);

	// LOG_INF("Starting aux pipe");
	// ret = video_stream_start(camera_aux_dev, VIDEO_BUF_TYPE_OUTPUT);
	// __ASSERT_NO_MSG(ret == 0);

	// LOG_INF("STARTING");

	// canvas = lv_canvas_create(lv_scr_act());
	// while (1) {
	// 	ret = video_dequeue(video_dev, &vbuf, K_FOREVER);
	// 	__ASSERT_NO_MSG(ret == 0);

	// 	lv_canvas_set_buffer(canvas, vbuf->buffer, DISPLAY_WIDTH, DISPLAY_HEIGHT, LV_COLOR_FORMAT_RGB565);
	// 	decorate_canvas(canvas);
	// 	lv_timer_handler();

	// 	ret = video_enqueue(video_dev, vbuf);
	// 	__ASSERT_NO_MSG(ret == 0);
	// }

	static const char *disk_mount_pt = DISK_MOUNT_PT;
	mp.mnt_point = disk_mount_pt;

	// mp.mnt_point = DISK_MOUNT_PT;
	LOG_INF("Mount point configured as: %s", mp.mnt_point);

	int res = fs_mount(&mp);
	LOG_INF("fs_mount res=%d", res);
	if (res != FR_OK)
	{
		LOG_ERR("Mount failed");
		return 0;
	}
	const char *dir_path = "/SD:/BIN_224";

	ls_dir(dir_path);

	/* Run slideshow: 1000 ms per image */
	// run_slideshow(img, dir_path, 1000);

	// /* keep mounted */
	// while (1) {
	// 	lv_timer_handler();
	// 	k_sleep(K_SECONDS(1));
	// }
	return 0;
}
