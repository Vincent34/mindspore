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
"""Test graph list inplace operation"""
import pytest
import numpy as np

from mindspore import Tensor, jit, context
from mindspore.common import mutable
from mindspore.ops.operations import _sequence_ops as seq

context.set_context(mode=context.GRAPH_MODE)

global_list_1 = [1, 2, 3, 4]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_global_list_used_in_graph():
    """
    Feature: Enable list inplace operation
    Description: List passed as global should not change the python obj
    Expectation: No exception.
    """
    @jit
    def foo():
        return global_list_1

    res = foo()
    assert id(res) == id(global_list_1)


global_float_list_1 = [1.0, 2.0, 3.0, 4.0]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_global_list_used_in_graph_2():
    """
    Feature: Enable list inplace operation
    Description: List passed as global should not change the python obj
    Expectation: No exception.
    """
    @jit
    def foo():
        return global_float_list_1

    res = foo()
    assert id(res) == id(global_float_list_1)


global_numpy_list = [np.array([1, 2, 3]), np.array([4, 5, 6])]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_global_numpy_list_used_in_graph():
    """
    Feature: Enable list inplace operation
    Description: List passed as global should not change the python obj
    Expectation: No exception.
    """
    @jit
    def foo():
        return global_numpy_list

    res = foo()
    assert id(res) == id(global_numpy_list)


global_list_2 = [1, 2, 3, 4, [3, 4], None]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_global_nested_list_getitem_in_graph():
    """
    Feature: Enable list inplace operation
    Description: List passed as global should not change the python obj
    Expectation: No exception.
    """
    @jit
    def foo():
        return global_list_2[4]

    res = foo()
    assert id(res) == id(global_list_2[4])


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_global_nested_list_return_in_graph():
    """
    Feature: Enable list inplace operation
    Description: List passed as global should not change the python obj
    Expectation: No exception.
    """
    @jit
    def foo():
        return global_list_2

    res = foo()
    assert id(res) == id(global_list_2)


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_global_nested_list_return_in_graph_2():
    """
    Feature: Enable list inplace operation
    Description: List passed as global should not change the python obj
    Expectation: No exception.
    """
    @jit
    def foo():
        return global_list_2, global_list_2[4]

    res = foo()
    assert len(res) == 2
    assert id(res[0]) == id(global_list_2)
    assert id(res[1]) == id(global_list_2[4])


global_list_3 = [1, 2, 3, (4, [3, 4])]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_global_nested_list_getitem_in_graph_2():
    """
    Feature: Enable list inplace operation
    Description: List passed as global should not change the python obj
    Expectation: No exception.
    """
    @jit
    def foo():
        return global_list_3[3][1]

    res = foo()
    assert id(res) == id(global_list_3[3][1])


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_global_nested_list_return_in_graph_3():
    """
    Feature: Enable list inplace operation
    Description: List passed as global should not change the python obj
    Expectation: No exception.
    """
    @jit
    def foo():
        return global_list_3, global_list_3[3][1]

    res = foo()
    assert len(res) == 2
    assert id(res[0]) == id(global_list_3)
    assert id(res[1]) == id(global_list_3[3][1])


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_return_local_list():
    """
    Feature: Enable list inplace operation
    Description: List create inside graph should be converted to PyExecute.
    Expectation: No exception.
    """
    @jit
    def foo(a, b):
        x = [1, 2, 3, a, b]
        return x

    input_a = Tensor([1])
    input_b = 2
    ret = foo(input_a, input_b)
    assert isinstance(ret, list)
    assert ret == [1, 2, 3, Tensor([1]), 2]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_return_local_list_2():
    """
    Feature: Enable list inplace operation
    Description: List create inside graph should be converted to PyExecute.
    Expectation: No exception.
    """
    @jit
    def foo(a, b):
        x = [1, 2, 3, a, b]
        return x

    input_a = Tensor([1])
    input_b = [1, 2]
    ret = foo(input_a, input_b)
    assert isinstance(ret, list)
    assert ret == [1, 2, 3, Tensor([1]), [1, 2]]


global_list_4 = [1, 2]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_return_local_list_3():
    """
    Feature: Enable list inplace operation
    Description: List create inside graph should be converted to PyExecute.
    Expectation: No exception.
    """
    @jit
    def foo(a):
        x = [1, 2, 3, a, global_list_4]
        return x

    input_a = Tensor([1])
    ret = foo(input_a)
    assert isinstance(ret, list)
    assert ret == [1, 2, 3, Tensor([1]), [1, 2]]
    assert id(ret[4]) == id(global_list_4)


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_return_local_list_4():
    """
    Feature: Enable list inplace operation
    Description: List create inside graph should be converted to PyExecute.
    Expectation: No exception.
    """
    @jit
    def foo(a, b):
        x = [1.0, 2, 3.0, a, b]
        return x

    input_a = Tensor([1])
    input_b = 2
    ret = foo(input_a, input_b)
    assert isinstance(ret, list)
    assert ret == [1.0, 2, 3.0, Tensor([1]), 2]


@pytest.mark.skip(reason="No need to convert to PyExecute node. SequenceMul execute fail in Ascend.")
@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_local_sequence_used_in_graph_with_operator():
    """
    Feature: Enable list inplace operation
    Description: List create inside graph should be converted to PyExecute, should be used as input to ops correctly.
    Expectation: No exception.
    """
    seq_func = seq.SequenceMul()

    @jit
    def foo(x, y):
        list_input = [x, y]
        return seq_func(list_input, 2)

    res = foo(Tensor([1]), Tensor([2]))
    assert isinstance(res, list)
    assert res == [Tensor([1]), Tensor([2]), Tensor([1]), Tensor([2])]


global_list_for_reverse = [1, 2, 3, 4]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_reverse():
    """
    Feature: list reverse.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        global_list_for_reverse.reverse()
        return global_list_for_reverse

    out = foo()
    assert id(out) == id(global_list_for_reverse)
    assert out == [4, 3, 2, 1]


global_list_for_reverse_2 = [Tensor([1, 2, 3]), Tensor([1, 2])]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_reverse_2():
    """
    Feature: list reverse.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        global_list_for_reverse_2.reverse()
        return global_list_for_reverse_2

    out = foo()
    assert id(out) == id(global_list_for_reverse_2)
    assert isinstance(out, list)
    assert len(out) == 2
    assert np.all(out[0].asnumpy() == np.array([1, 2]))
    assert np.all(out[1].asnumpy() == np.array([1, 2, 3]))


global_list_for_reverse_3 = ["1", np.array([1, 2, 3]), [1, 2], Tensor([1, 2, 3])]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_reverse_3():
    """
    Feature: list reverse.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        global_list_for_reverse_3.reverse()
        return global_list_for_reverse_3

    out = foo()
    assert id(out) == id(global_list_for_reverse_3)
    assert isinstance(out, list)
    assert len(out) == 4
    assert np.all(out[0].asnumpy() == np.array([1, 2, 3]))
    assert out[1] == [1, 2]
    assert np.all(out[2] == np.array([1, 2, 3]))
    assert out[3] == "1"


global_list_for_reverse_4 = [[1, 2], [3, 4]]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_reverse_element():
    """
    Feature: list reverse.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = global_list_for_reverse_4[0]
        x.reverse()
        return x

    out = foo()
    assert id(out) == id(global_list_for_reverse_4[0])
    assert out == [2, 1]
    assert global_list_for_reverse_4 == [[2, 1], [3, 4]]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_reverse_local_list():
    """
    Feature: list reverse.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = [[1, 2, 3], [4, 5]]
        y = x[0]
        y.reverse()
        return x, y

    out = foo()
    assert isinstance(out, tuple)
    assert len(out) == 2
    assert out[0] == [[3, 2, 1], [4, 5]]
    assert out[1] == [3, 2, 1]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_reverse_local_list_2():
    """
    Feature: list reverse.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = [[1, 2, 3], [4, 5]]
        y = x[0]
        y.reverse()
        return x

    out = foo()
    assert out == [[3, 2, 1], [4, 5]]


global_list_for_pop = [1, 2, 3, 4]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_pop():
    """
    Feature: list pop.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = global_list_for_pop.pop()
        return x, global_list_for_pop

    out = foo()
    assert isinstance(out, tuple)
    assert len(out) == 2
    assert out[0] == 4
    assert out[1] == [1, 2, 3]
    assert id(out[1]) == id(global_list_for_pop)


global_list_for_pop_2 = [1, [2, 3], "4"]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_pop_2():
    """
    Feature: list pop.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = global_list_for_pop_2.pop()
        return x, global_list_for_pop_2

    out = foo()
    assert isinstance(out, tuple)
    assert len(out) == 2
    assert out[0] == "4"
    assert out[1] == [1, [2, 3]]
    assert id(out[1]) == id(global_list_for_pop_2)


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_pop_local():
    """
    Feature: list pop.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = [[1, 2, 3], 4, 5]
        y = x[0]
        z = y.pop()
        return x, y, z

    out = foo()
    assert isinstance(out, tuple)
    assert len(out) == 3
    assert out[0] == [[1, 2], 4, 5]
    assert out[1] == [1, 2]
    assert out[2] == 3


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_pop_local_2():
    """
    Feature: list pop.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = ([1, 2, 3], 4, 5)
        y = x[0]
        z = y.pop()
        return x, y, z

    out = foo()
    assert isinstance(out, tuple)
    assert len(out) == 3
    assert out[0] == ([1, 2], 4, 5)
    assert out[1] == [1, 2]
    assert out[2] == 3


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_pop_local_3():
    """
    Feature: list pop.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = [[1, 2, 3], 4, 5]
        y = x[0]
        y.pop()
        return x

    out = foo()
    assert out == [[1, 2], 4, 5]


global_list_for_pop_extend = [1, 2, 3, 4]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_extend():
    """
    Feature: list extend.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        global_list_for_pop_extend.extend([1, 2, 3])
        return global_list_for_pop_extend

    out = foo()
    assert out == [1, 2, 3, 4, 1, 2, 3]
    assert id(out) == id(global_list_for_pop_extend)


global_list_for_pop_extend_2 = [1, 2, 3, 4]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_extend_no_return():
    """
    Feature: list extend.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        global_list_for_pop_extend_2.extend([1, 2, 3])

    foo()
    assert global_list_for_pop_extend_2 == [1, 2, 3, 4, 1, 2, 3]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_extend_local_list():
    """
    Feature: list extend.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = [0, [1, 2, 3], 4, 5]
        y = x[1]
        y.extend(("a", 1))
        return x, y

    out = foo()
    assert isinstance(out, tuple)
    assert len(out) == 2
    assert out[0] == [0, [1, 2, 3, "a", 1], 4, 5]
    assert out[1] == [1, 2, 3, "a", 1]


global_list_for_pop_insert = [1, 2, 3, 4]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_insert():
    """
    Feature: list insert.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        global_list_for_pop_insert.insert(-2, [1, 2, 3])
        return global_list_for_pop_insert

    out = foo()
    assert out == [1, 2, [1, 2, 3], 3, 4]
    assert id(out) == id(global_list_for_pop_insert)


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_insert_local_list():
    """
    Feature: list extend.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = [0, [1, 2, 3], 4, 5]
        y = x[1]
        y.insert(0, ("a", "b"))
        return x, y

    out = foo()
    assert isinstance(out, tuple)
    assert len(out) == 2
    assert out[0] == [0, [("a", "b"), 1, 2, 3], 4, 5]
    assert out[1] == [("a", "b"), 1, 2, 3]


@pytest.mark.skip(reason="When index input is variable, insert will run dynamic shape ListInsert operator.")
@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_insert_local_list_2():
    """
    Feature: list extend.
    Description: support list reverse.
    Expectation: No exception.
    """
    @jit
    def foo(a):
        x = [0, [1, 2, 3], 4, 5]
        y = x[1]
        y.insert(a, ("a", "b"))
        return x, y

    out = foo(mutable(0))
    assert isinstance(out, tuple)
    assert len(out) == 2
    assert out[0] == [0, [("a", "b"), 1, 2, 3], 4, 5]
    assert out[1] == [("a", "b"), 1, 2, 3]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_reverse_with_variable():
    """
    Feature: list inplace ops.
    Description: support list inplace ops.
    Expectation: No exception.
    """
    @jit
    def foo(a, b):
        x = [a, b]
        x.reverse()
        return x

    a = Tensor([1, 2, 3])
    b = Tensor([4, 5, 6])
    out = foo(a, b)
    assert isinstance(out, list)
    assert len(out) == 2
    assert np.all(out[0].asnumpy() == np.array([4, 5, 6]))
    assert np.all(out[1].asnumpy() == np.array([1, 2, 3]))


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_reverse_with_variable_2():
    """
    Feature: list inplace ops.
    Description: support list inplace ops.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = mutable([1, 2, 3])
        x.reverse()
        return x

    out = foo()
    assert out == [3, 2, 1]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_reverse_with_variable_3():
    """
    Feature: list inplace ops.
    Description: support list inplace ops.
    Expectation: No exception.
    """
    @jit
    def foo(x):
        x.reverse()
        return x

    x = mutable([1, 2, 3])
    out = foo(x)
    assert out == [3, 2, 1]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_dynamic_len_list_inplace_op():
    """
    Feature: dynamic length list do not run inplace operation.
    Description: support list inplace ops.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = [2, 5, 6, 7]
        y = mutable(2)
        x = mutable(x, True)
        x[y] = 1
        return x

    out = foo()
    assert out == [2, 5, 1, 7]


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_dynamic_len_list_inplace_op_2():
    """
    Feature: dynamic length list do not run inplace operation.
    Description: support list inplace ops.
    Expectation: No exception.
    """
    @jit
    def foo(a):
        x = [1, 2, 3, 4]
        y = x[a::]
        return y

    input_index = Tensor(2)
    out = foo(input_index)
    assert out == [3, 4]


global_list_all_str = ['a', 'b', 'c']


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_with_all_str():
    """
    Feature: dynamic length list do not run inplace operation.
    Description: support list inplace ops.
    Expectation: No exception.
    """
    @jit
    def foo():
        global_list_all_str.extend(['d', 'e'])
        return global_list_all_str

    out = foo()
    assert id(out) == id(global_list_all_str)
    assert global_list_all_str == ['a', 'b', 'c', 'd', 'e']


global_tuple_with_list_all_str = (['a', 'b', 'c'], 1, 2)


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_inplace_with_all_str_2():
    """
    Feature: dynamic length list do not run inplace operation.
    Description: support list inplace ops.
    Expectation: No exception.
    """
    @jit
    def foo():
        x = global_tuple_with_list_all_str[0]
        x.extend(['d', 'e'])
        return x

    out = foo()
    assert id(global_tuple_with_list_all_str[0]) == id(out)
    assert global_tuple_with_list_all_str == (['a', 'b', 'c', 'd', 'e'], 1, 2)


@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_list_in_joined_str():
    """
    Feature: dynamic length list do not run inplace operation.
    Description: support list inplace ops.
    Expectation: No exception.
    """
    @jit
    def foo(x, y, z):
        if z >= 1:
            raise TypeError(f"The input is {x}")
        raise TypeError(f"The input is {y}")

    x = mutable([1, 2, 3])
    y = mutable([4, 5])
    z = Tensor([1])
    with pytest.raises(TypeError) as raise_info:
        foo(x, y, z)
    assert "The input is [1, 2, 3]" in str(raise_info.value)
