# Copyright 2022 Huawei Technologies Co., Ltd
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
import numpy as np
import pytest

import mindspore.context as context
import mindspore.common.dtype as mstype
from mindspore.ops import deformable_conv2d
from mindspore import Tensor

context.set_context(device_target="Ascend")


@pytest.mark.level1
@pytest.mark.platform_x86_ascend_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.env_onecard
def test_deformable_conv2d():
    """"
    Feature: deformable_conv2d function
    Description: Test case for simplest deformable_conv2d
    Expectation: The results are as expected
    """
    kh, kw = 1, 1
    deformable_group = 1
    stride_h, stride_w = 1, 1
    pad_h, pad_w = 0, 0
    dilation_h, dilation_w = 1, 1
    # x shape [1, 8, 1, 2]
    x = np.array([[[[-0.41675785, -0.05626683]],
                   [[1.41675785, -0.25626683]],
                   [[-0.41675785, 1.79979878]],
                   [[1.41634355, -0.05626683]],
                   [[-0.5475785, -2.9879797]],
                   [[0.4543585, -0.9792279]],
                   [[-1.5435465, -0.79898799]],
                   [[0.5645654, 0.4656564]]]]).astype(np.float32)
    x = Tensor(x, mstype.float32)
    # weight shape [1, 8, 1, 1]
    weight = np.array([[[[-2.1361961]],
                        [[-1.767576]],
                        [[0.454354]],
                        [[-2.1361961]],
                        [[0.56756]],
                        [[-0.80899]],
                        [[0.767676]],
                        [[-1.675756]]]]).astype(np.float32)
    weight = Tensor(weight, mstype.float32)
    # offsets shape [1, 3, 1, 2]
    offsets = np.array([[[[1.6402708, -1.7934356]],
                         [[-0.84174734, 0.5028814]],
                         [[-1.2452881, -1.0579522]]]]).astype(np.float32)
    offsets = Tensor(offsets, mstype.float32)
    out = deformable_conv2d(x, weight, offsets, (kh, kw), (1, 1, stride_h, stride_w), (pad_h, pad_h, pad_w, pad_w),
                            data_format="NCHW", dilations=(1, 1, dilation_h, dilation_w),
                            deformable_groups=deformable_group)
    # expected output: [1, 1, 1, 2]
    expected = np.array([[[[0.05569747, 0.82974637]]]]).astype(np.float32)
    assert np.allclose(out.asnumpy(), expected, 0.0001, 0.0001)
