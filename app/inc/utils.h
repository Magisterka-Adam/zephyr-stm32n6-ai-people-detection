/**
 ******************************************************************************
 * @file    utils.h
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

#ifndef APP_UTILS_H
#define APP_UTILS_H

#include "model.h"

#define MAX_GT_BOXES 64

struct gt_boxes {
	int count;
	float xywh[MAX_GT_BOXES][4];
};

struct box_xyxy {
	float x0, y0, x1, y1;
};

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

void rstrip(char *s);
void lskip(char **ps);
float iou_xyxy(struct box_xyxy a, struct box_xyxy b);
int gt_find_person_boxes_csv(const char *csv_path, const char *filename, struct gt_boxes *out);
int count_files_in_dir(const char *path);
int count_csv_lines(const char *csv_path);

#endif /* APP_UTILS_H */
