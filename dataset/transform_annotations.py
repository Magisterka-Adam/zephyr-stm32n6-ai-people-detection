import json
from collections import defaultdict

ANN_FILE = "coco/annotations/instances_val2017.json"
OUT = "coco/benchmark_256.csv"
SIZE = 256  # your BIN size (256x256)

with open(ANN_FILE, "r") as f:
    coco = json.load(f)

# COCO image metadata: id -> (file_name, width, height)
id_to_meta = {
    im["id"]: (im["file_name"], float(im["width"]), float(im["height"]))
    for im in coco["images"]
}

img_to_boxes = defaultdict(list)

for ann in coco["annotations"]:
    if ann["category_id"] != 1:  # person only
        continue
    # bbox is [x, y, w, h] in original image pixels
    img_to_boxes[ann["image_id"]].append(ann["bbox"])

def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v

with open(OUT, "w") as f:
    for img_id, boxes in img_to_boxes.items():
        meta = id_to_meta.get(img_id)
        if not meta:
            continue
        jpg_name, W, H = meta

        sx = SIZE / W
        sy = SIZE / H

        # convert "000000xxxxxx.jpg" -> "000000xxxxxx.bin"
        bin_name = jpg_name.rsplit(".", 1)[0] + ".bin"

        flat = []
        kept = 0
        for (x, y, w, h) in boxes:
            # scale to 224 space
            x *= sx
            y *= sy
            w *= sx
            h *= sy

            # clamp to image bounds
            x = clamp(x, 0.0, SIZE - 1.0)
            y = clamp(y, 0.0, SIZE - 1.0)
            w = clamp(w, 0.0, SIZE - x)
            h = clamp(h, 0.0, SIZE - y)

            # drop degenerate boxes
            if w < 1.0 or h < 1.0:
                continue

            flat += [x, y, w, h]
            kept += 1

        if kept == 0:
            continue

        f.write(bin_name + "," + str(kept) + "," + ",".join(f"{v:.3f}" for v in flat) + "\n")

print("Wrote:", OUT)
