/**
 ******************************************************************************
 * @file    utils.c
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
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>

#include "utils.h"

LOG_MODULE_REGISTER(utils);

void rstrip(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' ||
				 s[n - 1] == ' ' || s[n - 1] == '\t'))
	{
		s[--n] = 0;
	}
}

void lskip(char **ps)
{
	while (**ps == ' ' || **ps == '\t')
	{
		(*ps)++;
	}
}

float iou_xyxy(struct box_xyxy a, struct box_xyxy b)
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

int gt_find_person_boxes_csv(const char *csv_path,
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

int count_files_in_dir(const char *path)
{
	struct fs_dir_t dirp;
	struct fs_dirent entry;
	int count = 0;
	int res;

	fs_dir_t_init(&dirp);

	res = fs_opendir(&dirp, path);
	if (res)
	{
		LOG_ERR("fs_opendir(%s) failed: %d", path, res);
		return res;
	}

	while (1)
	{
		res = fs_readdir(&dirp, &entry);
		if (res || entry.name[0] == 0)
		{
			break; /* end of dir */
		}

		if (entry.type == FS_DIR_ENTRY_FILE)
		{
			count++;
		}
	}

	fs_closedir(&dirp);
	return count;
}

int count_csv_lines(const char *csv_path)
{
	struct fs_file_t file;
	uint8_t ch;
	int lines = 0;
	int ret;

	fs_file_t_init(&file);

	ret = fs_open(&file, csv_path, FS_O_READ);
	if (ret < 0)
	{
		LOG_ERR("fs_open(%s) failed: %d", csv_path, ret);
		return ret;
	}

	while (fs_read(&file, &ch, 1) == 1)
	{
		if (ch == '\n')
		{
			lines++;
		}
	}

	fs_close(&file);
	return lines;
}
