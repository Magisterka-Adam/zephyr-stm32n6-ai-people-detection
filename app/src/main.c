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
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>

#include <lvgl.h>
#include <font/lv_font.h>
#include <lvgl_mem.h>
#include <lvgl_zephyr.h>

#include <stdarg.h>

#include "model.h"
#include "utils.h"

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

static int img_with_gt = 0;
static int img_with_pred = 0;
static int gt_matched = 0;
static int pred_matched = 0;

static int bench_total = 0;
static int bench_done = 0;
static int bench_errors = 0;

static int bench_last_ms = 0;
static int bench_min_ms = 0x7fffffff;
static int bench_max_ms = 0;
static int64_t bench_sum_ms = 0;
static int bench_samples = 0;
static uint16_t canvas_fb[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static struct video_buffer *sd_vbuf;

void nn_init();
void run_nn_from_sd_card(uint8_t *image);

/* Semaphore signaled on evt->sync */
K_SEM_DEFINE(sync, 0, 1);

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

static void draw_progress_bar(lv_layer_t *layer, int x, int y, int w, int h, float percent)
{
	lv_draw_rect_dsc_t box, fill;
	lv_area_t a;

	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;

	lv_draw_rect_dsc_init(&box);
	box.border_width = 2;
	box.border_color = lv_color_white();
	box.radius = 6;
	box.bg_opa = LV_OPA_10;

	a.x1 = x;
	a.y1 = y;
	a.x2 = x + w;
	a.y2 = y + h;
	lv_draw_rect(layer, &box, &a);

	lv_draw_rect_dsc_init(&fill);
	fill.radius = 6;
	fill.bg_opa = LV_OPA_COVER;
	fill.bg_color = lv_palette_main(LV_PALETTE_BLUE);
	fill.border_width = 0;

	int fw = (int)((w - 4) * (percent / 100.0f));
	if (fw < 0)
		fw = 0;
	a.x1 = x + 2;
	a.y1 = y + 2;
	a.x2 = x + 2 + fw;
	a.y2 = y + h - 2;
	if (a.x2 > a.x1)
	{
		lv_draw_rect(layer, &fill, &a);
	}
}

static void clear_canvas(lv_obj_t *canvas)
{
	/* Fast clear to black (change to lv_color_white() if you want) */
	lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
}

static void decorate_benchmark(lv_obj_t *canvas, float percent)
{
	clear_canvas(canvas);

	const lv_font_t *font = &lv_font_unscii_16;
	lv_layer_t layer;
	uint32_t ver = sys_kernel_version_get();
	int load = 0;

	lv_canvas_init_layer(canvas, &layer);

	print_text(&layer, 0, 0, LV_TEXT_ALIGN_CENTER, "Benchmark: YOLOv2");
	print_text(&layer, 0, font->line_height, LV_TEXT_ALIGN_CENTER,
			   "Zephyr %d.%d.%d", SYS_KERNEL_VER_MAJOR(ver), SYS_KERNEL_VER_MINOR(ver), SYS_KERNEL_VER_PATCHLEVEL(ver));

	/* Progress bar */
	int bar_w = DISPLAY_WIDTH - 40;
	int bar_h = 18;
	int bar_x = 20;
	int bar_y = font->line_height * 3;
	draw_progress_bar(&layer, bar_x, bar_y, bar_w, bar_h, percent);

	print_text(&layer, 0, bar_y + bar_h + 4, LV_TEXT_ALIGN_CENTER,
			   "%d / %d (%.1f%%)", bench_done, bench_total, (double)percent);

	/* Latency stats */
	int avg = (bench_samples > 0) ? (int)(bench_sum_ms / bench_samples) : 0;
	int fps = (bench_last_ms > 0) ? (1000 / bench_last_ms) : 0;

	int y0 = DISPLAY_HEIGHT - font->line_height * 6;
	print_text(&layer, 0, y0 + font->line_height * 0, LV_TEXT_ALIGN_LEFT,
			   "Last: %d ms  (%d FPS)", bench_last_ms, fps);
	print_text(&layer, 0, y0 + font->line_height * 1, LV_TEXT_ALIGN_LEFT,
			   "Avg : %d ms", avg);
	print_text(&layer, 0, y0 + font->line_height * 2, LV_TEXT_ALIGN_LEFT,
			   "Min : %d ms   Max: %d ms", (bench_min_ms == 0x7fffffff) ? 0 : bench_min_ms, bench_max_ms);

#ifdef CONFIG_CPU_LOAD
	load = cpu_load_get(1);
	print_text(&layer, 0, y0 + font->line_height * 3, LV_TEXT_ALIGN_LEFT,
			   "CPU : %d.%d%%", load / 10, load % 10);
#endif

	/* Your existing detection benchmark counters */
	print_text(&layer, 0, y0 + font->line_height * 4, LV_TEXT_ALIGN_LEFT,
			   "GT:%d  Pred:%d  MatchGT:%d  MatchPred:%d",
			   img_with_gt, img_with_pred, gt_matched, pred_matched);

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

static int run_benchmark_on_sd_card(const char *path, lv_obj_t *canvas)
{
	int res;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;
	int count = 0;
	bench_total = count_csv_lines("/SD:/benchmark_224.csv");
	if (bench_total <= 0)
	{
		LOG_ERR("No files found for benchmark");
		return 0;
	}

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
		bench_last_ms = model_get_latest_inference_time();
		if (bench_last_ms > 0)
		{
			bench_min_ms = MIN(bench_min_ms, bench_last_ms);
			bench_max_ms = MAX(bench_max_ms, bench_last_ms);
			bench_sum_ms += bench_last_ms;
			bench_samples++;
		}
		else
		{
			bench_errors++;
		}
		bench_done = count;

		float percent = 100.0f * (float)count / (float)bench_total;
		decorate_benchmark(canvas, percent);
		lv_timer_handler();
		k_sleep(K_MSEC(5));

	}
	LOG_INF("END OF BENCHMARK");

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
static bool pressed;

static void button_input_cb(struct input_event *evt, void *user_data)
{
	if (evt->sync == 0)
	{
		return;
	}

	if (evt->value == 0)
	{
		LOG_INF("Button released - Benchmark started!!!!");
		pressed = 1;
	}
}

INPUT_CALLBACK_DEFINE(NULL, button_input_cb, NULL);

int main()
{
	const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	lv_obj_t *canvas;
	k_tid_t nn_tid;
	int ret;

	__ASSERT_NO_MSG(device_is_ready(display_dev));

	/* Configure display */
	ret = display_setup(display_dev);
	__ASSERT_NO_MSG(ret == 0);

	nn_init();
	sd_vbuf = video_buffer_aligned_alloc(NN_HEIGHT * NN_WIDTH * NN_BPP, 32, K_FOREVER);
	__ASSERT_NO_MSG(sd_vbuf && sd_vbuf->buffer);

	canvas = lv_canvas_create(lv_scr_act());
	lv_canvas_set_buffer(canvas, canvas_fb, DISPLAY_WIDTH, DISPLAY_HEIGHT, LV_COLOR_FORMAT_RGB565);
	lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER); /* optional */

	lv_obj_t *start_label = lv_label_create(lv_scr_act());
	lv_label_set_text(start_label,
					  "Press Button 'USER 1' to start benchmark");
	lv_obj_set_width(start_label, DISPLAY_WIDTH - 20);

	/* Match print_text() font and alignment */
	lv_obj_set_style_text_font(start_label, &lv_font_unscii_16, 0);
	lv_obj_set_style_text_align(start_label, LV_TEXT_ALIGN_CENTER, 0);

	lv_obj_align(start_label, LV_ALIGN_CENTER, 0, 0);

	while (!pressed)
	{
		lv_timer_handler();
		k_sleep(K_MSEC(10));
	}

	lv_obj_del(start_label);
	lv_timer_handler(); /* flush deletion */

	mp.mnt_point = DISK_MOUNT_PT;

	LOG_INF("Mount point configured as: %s", mp.mnt_point);

	int res = fs_mount(&mp);
	LOG_INF("fs_mount res=%d", res);
	if (res != FR_OK)
	{
		LOG_ERR("Mount failed");
		return 0;
	}
	const char *dir_path = "/SD:/BIN_224";

	run_benchmark_on_sd_card(dir_path, canvas);

	return 0;
}
