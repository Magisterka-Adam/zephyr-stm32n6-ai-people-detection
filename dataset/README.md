# Prepare Dataset for Benchmark

## Download COCO Dataset
Using command:
```bash
bash get_dataset.sh
```
You get downloaded COCO validation 2017 images and annotations.

## Prepare dataset for stm32n6 local benchmark
### Images
To transform images to binaries ready for embedded processing create python environment
```bash
python3 -m venv env
source env/bin/activate
pip install -r requirements.txt
```
Use python script to transform images to binaries:
```bash
python3 transform_dataset_to_bin.py 
```

### Annotations
Use python script to transform json anotations to csv read by stm32n6:
```bash
python3 transform_dataset_to_bin.py 
```