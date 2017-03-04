#!/usr/bin/env sh


./build/tools/caffe train --gpu=0 \
    --solver=models/nuswide/multilabel/image_solver.prototxt \
    --weights=models/bvlc_reference_caffenet/bvlc_reference_caffenet.caffemodel
