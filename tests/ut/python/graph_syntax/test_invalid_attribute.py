# Copyright 2023 Huawei Technologies Co., Ltd
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
""" test jit forbidden api in graph mode. """
import pytest

import mindspore.nn as nn
import mindspore.common.dtype as mstype
from mindspore import context, Tensor, Parameter, ops

context.set_context(mode=context.GRAPH_MODE)


@pytest.mark.level0
@pytest.mark.platform_x86_cpu
@pytest.mark.env_onecard
def test_tensor_invalid_attr():
    """
    Feature: Tensor.abcd.
    Description: Graph syntax object's invalid attribute and method.
    Expectation: AttributeError exception raise.
    """
    class Net(nn.Cell):
        def construct(self, x):
            x.abcd()
            return x

    x = Tensor([1, 2, 3], dtype=mstype.float32, const_arg=True)
    net = Net()
    with pytest.raises(AttributeError) as ex:
        net(x)
    assert "'Tensor' object has no attribute 'abcd'" in str(ex.value)


def test_int_invalid_attr():
    """
    Feature: Int.shape.
    Description: Get shape attr from int object, expect raise exception.
    Expectation: AttributeError exception raise.
    """

    class ShapeNet(nn.Cell):
        def __init__(self):
            super().__init__()

        def construct(self, x):
            return x.shape

    with pytest.raises(AttributeError) as err_info:
        ShapeNet()(1)
    assert "'Int' object has no attribute 'shape'" in str(err_info.value)


def test_create_parameter_instance():
    """
    Feature: Create Parameter instance.
    Description: Create Parameter instance in graph mode, expect raise exception.
    Expectation: ValueError exception raise.
    """

    class Net(nn.Cell):
        def __init__(self):
            super().__init__()

        def construct(self, x):
            return Parameter(x, name="weight")

    with pytest.raises(ValueError):
        x = Tensor([1, 2, 3])
        net = Net()
        net(x)


def test_create_multitype_funcgraph_instance():
    """
    Feature: Create MultitypeFuncGraph instance.
    Description: Create MultitypeFuncGraph instance in graph mode, expect raise exception.
    Expectation: ValueError exception raise.
    """

    class Net(nn.Cell):
        def construct(self, x):
            add = ops.MultitypeFuncGraph('add')
            return add(x, 1)

    with pytest.raises(ValueError):
        x = Tensor([1])
        net = Net()
        net(x)
