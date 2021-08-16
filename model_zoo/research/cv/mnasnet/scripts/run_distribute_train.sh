#!/bin/bash
# Copyright 2021 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================

PATH1=$1
PATH2=$2

ulimit -u unlimited
export DEVICE_NUM=8
export RANK_SIZE=8
export RANK_TABLE_FILE=$PATH1
export HCCL_CONNECT_TIMEOUT=1200

for ((i = 0; i < ${DEVICE_NUM}; i++)); do
  let deviceID=$i
  echo $deviceID
  export DEVICE_ID=$deviceID
  export RANK_ID=$i
  rm -rf ./train_parallel$i
  mkdir ./train_parallel$i
  cp ../*.py ./train_parallel$i
  cp *.sh ./train_parallel$i
  cp -r ../src ./train_parallel$i
  cd ./train_parallel$i || exit
  echo "start training for rank $RANK_ID, device $DEVICE_ID"
  env >env.log
  python -u train.py --run_distribute=True --dataset_path=$PATH2 > log.txt 2>&1 &
  cd ..
done
