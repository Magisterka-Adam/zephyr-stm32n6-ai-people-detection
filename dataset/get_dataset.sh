#!/bin/bash
# Download dataset
wget http://images.cocodataset.org/zips/val2017.zip

# Unzip all dataset
mkdir coco \
    && unzip val2017.zip \
    && rm val2017.zip \
    && mv val2017/ coco/

## Get annotations
wget http://images.cocodataset.org/annotations/annotations_trainval2017.zip \
    && unzip annotations_trainval2017.zip \
    && rm annotations_trainval2017.zip \
    && mv annotations/ coco