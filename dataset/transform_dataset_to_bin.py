# save_image_to_raw.py
from PIL import Image
import numpy as np
from pathlib import Path

IMAGE_DIRECTORY = "coco/val2017"
TARGET_DIRECTORY = "coco/bin_2017_256"
TARGET_SIZE = (256, 256)            # width, height

directory = Path(IMAGE_DIRECTORY)

def save_bin(file: str):
    img = Image.open(file).convert("RGB")
    img = img.resize(TARGET_SIZE, Image.BILINEAR)  
    # Convert to numpy array
    arr = np.array(img, dtype=np.uint8)

    new_name = file.replace("jpg","bin")
    new_name = new_name.replace(IMAGE_DIRECTORY+"/", "")

    arr.tofile(f"{TARGET_DIRECTORY}/{new_name}")
    print(f"Saved file {new_name}.bin | Size: {arr.size} bytes")

for file in directory.glob("*.jpg"):
    save_bin(str(file))

print("All files had been saved successfully!")
