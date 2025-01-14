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
""" test graph JIT Fallback runtime feature """

import pytest
import numpy as np
import mindspore as ms
from mindspore import Tensor
from mindspore.common.parameter import Parameter
from mindspore import jit
import mindspore.amp as amp

ms.set_context(mode=ms.GRAPH_MODE)


class UserDefinedNet:
    def __init__(self):
        self.value = 10

    def __call__(self, x):
        return self.value * x


class UNet(ms.nn.Cell):
    def __init__(self, net):
        super().__init__()
        self.net = net
        self.para = Parameter(Tensor(2, dtype=ms.float64), name='para')

    def construct(self, x):
        out = x * self.para
        print("out:", out)
        out = self.net(x) + self.para
        self.para = 2 * x
        return out, self.para + 10


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
@pytest.mark.skip("the PyExecute is in the other graph and the parameter not get user data")
def test_fallback_side_effect_assign():
    """
    Feature: Fallback runtime side effect.
    Description: Test execution order in Fallback runtime.
    Expectation: No error.
    """
    net = UNet(UserDefinedNet())
    x = np.array(10, np.float64)
    output = net(ms.Tensor(x))
    print("output:", output)
    assert output[0].asnumpy() == 102
    assert output[1].asnumpy() == 30


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_fallback_side_effect_dict():
    """
    Feature: Fallback runtime side effect.
    Description: Test execution order in Fallback runtime.
    Expectation: No error.
    """

    class Net(ms.nn.Cell):
        def __init__(self):
            super().__init__()
            self.para = Parameter(Tensor(2, dtype=ms.float64), name='para')
            self.para2 = Parameter(Tensor(4, dtype=ms.float64), name='para2')

        def construct(self, x):
            out = x * self.para
            print("out:", out)
            x = {'a': Tensor(1, dtype=ms.float64), 'b': Tensor(2, dtype=ms.float64)}
            y = x.get('a') + out
            z = dict(a=y + self.para - self.para2)
            self.para = 2 * y
            return z, self.para + 2

    net = Net()
    x = np.array(10, np.float64)
    out = net(ms.Tensor(x))
    print("out:", out)
    assert out[0] == {'a': 19}
    assert out[1] == 44


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_fallback_side_effect_dict_2():
    """
    Feature: Fallback runtime side effect.
    Description: Test execution order in Fallback runtime.
    Expectation: No error.
    """

    class Net(ms.nn.Cell):
        def __init__(self):
            super().__init__()
            self.para = Parameter(Tensor(2, dtype=ms.float64), name='para')

        def construct(self, x):
            out = x * self.para
            x = {'a': Tensor(1, dtype=ms.float64), 'b': Tensor(2, dtype=ms.float64)}
            self.para = x.get('a') + out
            out = x.get('b') - self.para
            y = {'c': 3, 'b': 4, 'd': self.para + 1}
            x.update(y)
            return self.para + out, x

    net = Net()
    x = np.array(10, np.float64)
    out = net(ms.Tensor(x))
    print("out:", out)
    assert out[0] == 2
    assert out[1] == {'a': 1, 'b': 4, 'c': 3, 'd': 22}


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_fallback_side_effect_nested_net():
    """
    Feature: Fallback runtime side effect.
    Description: Test execution order in Fallback runtime.
    Expectation: No error.
    """

    class Inner:
        def __init__(self):
            self.number = ms.Tensor(2, dtype=ms.float64)

        def act(self, x, y):
            return self.number * (x + y)

    @ms.jit_class
    class InnerNet:
        def __init__(self):
            self.inner = Inner()
            self.para = Parameter(Tensor(2, dtype=ms.float64), name='para')

        def renew_para(self, x, y):
            self.para = x + y
            return self.para

    class NestedNet(ms.nn.Cell):
        @ms.jit
        def construct(self, x, y):
            out = InnerNet().inner.act(InnerNet().renew_para(x, y) + x, y)
            out = out + InnerNet().renew_para(out, y)
            return out

    x = ms.Tensor(2, dtype=ms.float64)
    y = ms.Tensor(4, dtype=ms.float64)
    net = NestedNet()
    output = net(x, y)
    print("output:", output)
    assert output == 52


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_fallback_control_flow():
    """
    Feature: Fallback runtime side effect.
    Description: Test execution order in Fallback runtime.
    Expectation: No error.
    """

    class Net(ms.nn.Cell):
        def __init__(self):
            super().__init__()
            self.para = Parameter(Tensor(2, dtype=ms.float64), name='para')

        def construct(self, x):
            out = x * self.para
            x = {'a': Tensor(1, dtype=ms.float64), 'b': Tensor(2, dtype=ms.float64)}
            self.para = x.get('a')
            if self.para > 0:
                out = x.get('b') - self.para
            return self.para, out, x

    net = Net()
    x = np.array(10, np.float64)
    out = net(ms.Tensor(x))
    print("out:", out)
    assert out[0] == 1
    assert out[1] == 1
    assert out[2] == {'a': 1, 'b': 2}


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_fallback_side_effect_asnumpy():
    """
    Feature: Fallback runtime side effect.
    Description: Test execution order in Fallback runtime.
    Expectation: No error.
    """

    @jit
    def loss_scale():
        loss_scaler = amp.DynamicLossScaler(scale_value=2 ** 10, scale_factor=2, scale_window=1)
        grads = (Tensor(np.array([np.log(-1), 1.0]).astype(np.float32)), Tensor(np.array([0.2]).astype(np.float32)))
        unscaled_grads = loss_scaler.unscale(grads)
        is_finite = amp.all_finite(unscaled_grads)
        loss_scaler.adjust(is_finite)
        return loss_scaler.scale_value.asnumpy()

    out = loss_scale()
    assert out == 512


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_fallback_side_effect_assign_1():
    """
    Feature: Fallback runtime side effect.
    Description: Test execution order in Fallback runtime.
    Expectation: No error.
    """

    class Net(ms.nn.Cell):
        def __init__(self):
            super().__init__()
            self.para = Parameter(Tensor(2, dtype=ms.float64), name='para')

        def construct(self, x):
            out = x * self.para
            self.para = 2 * x
            return out, self.para.asnumpy()

    net = Net()
    x = np.array(10, np.float64)
    out = net(ms.Tensor(x))
    assert out[1] == 20


@pytest.mark.skip("the PyExecute is in the other graph and the parameter not get user data")
@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_fallback_side_effect_assign_2():
    """
    Feature: Fallback runtime side effect.
    Description: Test execution order in Fallback runtime.
    Expectation: No error.
    """

    class Net(ms.nn.Cell):
        def __init__(self):
            super().__init__()
            self.para = Parameter(Tensor(2, dtype=ms.float64), name='para')

        def construct(self, x):
            self.para = 2 * x
            return self.para.asnumpy()

    net = Net()
    x = np.array(10, np.float64)
    out = net(ms.Tensor(x))
    assert out == 20


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_fallback_side_effect_dict_3():
    """
    Feature: Fallback runtime side effect.
    Description: Test execution order in Fallback runtime.
    Expectation: No error.
    """
    class Net(ms.nn.Cell):
        def __init__(self):
            super().__init__()
            self.para = Parameter(Tensor(2, dtype=ms.float64), name='para')

        def construct(self, input_x):
            self.para = input_x + 1
            x = {'a': Tensor(1, dtype=ms.float64), 'b': self.para}
            self.para = x.get('a') * 2
            y = x.get('b') + self.para * 2
            self.para = self.para + 10
            z = {'c': 3, 'b': self.para * 2, 'd': self.para + 1}
            return x.get('b') + 1, y, z


    net = Net()
    x = np.array(10, np.float64)
    out = net(ms.Tensor(x))
    assert out[0] == 13
    assert out[1] == 6
    assert out[2] == {'c': 3, 'b': 24, 'd': 13}


class PrintPyExecuteNet(ms.nn.Cell):
    def __init__(self, net):
        super().__init__()
        self.net = net

    def construct(self, x):
        out = x * x
        print("out1:", out)
        out = self.net(x) + out
        print("out2:", out)
        return out


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_print_pyexecute():
    """
    Feature: Side effect in Fallback runtime.
    Description: Side effect in Fallback runtime.
    Expectation: No error.
    """
    net = PrintPyExecuteNet(UserDefinedNet())
    x = np.array([10], np.float64)
    output = net(ms.Tensor(x))
    print(output)
    assert output == 200


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_fallback_dtype_is_cond():
    """
    Feature: Side effect in Fallback runtime.
    Description: Side effect in Fallback runtime.
    Expectation: No error.
    """
    @ms.jit
    def func(x):
        dtype = x.dtype
        if dtype:
            return 0
        return 1

    x = ms.Tensor(True)
    out = func(x)
    assert out == 0


@pytest.mark.skip("the PyExecute is in the other graph and the parameter not get user data")
@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_if_after_for_in_if_numpy():
    """
    Feature: JIT Fallback
    Description: Test fallback with control flow.
    Expectation: No exception.
    """
    class UserDefinedNet2:
        def __init__(self):
            self.x = np.array([1, 2])
            self.y = np.array([3, 4])
            self.z = np.array([1, 2, 3, 4])

        def __call__(self, x):
            return self.x

    class PyExecuteNet(ms.nn.Cell):
        def __init__(self, net):
            super().__init__()
            self.net = net

        def construct(self):
            x = self.net.x
            y = self.net.y
            z = self.net.z
            if len(x) + len(y) == len(z):
                return Tensor(y)
            return Tensor(z)

    net = PyExecuteNet(UserDefinedNet2())
    output = net()
    assert (output.asnumpy() == [3, 4]).all()
