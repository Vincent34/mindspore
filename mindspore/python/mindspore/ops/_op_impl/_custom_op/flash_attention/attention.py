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
"""the base class of flash attention"""
from abc import ABCMeta
from abc import abstractmethod
from functools import partial
from collections import defaultdict

import te.platform as tbe_platform
from tbe import tik
from tbe.common.platform import get_soc_spec

from mindspore.ops._op_impl._custom_op.flash_attention.constants import FP16
from mindspore.ops._op_impl._custom_op.flash_attention.constants import FP32
from mindspore.ops._op_impl._custom_op.flash_attention.constants import GM
from mindspore.ops._op_impl._custom_op.flash_attention.constants import INT8
from mindspore.ops._op_impl._custom_op.flash_attention.constants import MASK_FILL_VALUE
from mindspore.ops._op_impl._custom_op.flash_attention.constants import UB
from mindspore.ops._op_impl._custom_op.flash_attention.tik_ops_utils import TikOpsUtils
from mindspore.ops._op_impl._custom_op.flash_attention.tiling_strategy.strategy import TilingPara
from mindspore.ops._op_impl._custom_op.flash_attention.tiling_strategy.strategy import TilingStrategy
from mindspore.ops._op_impl._custom_op.flash_attention.tiling_strategy.xunfei_tiling import XunfeiTiling


class FlashAttention(metaclass=ABCMeta):
    """The base class of FlashAttention"""

    def __init__(self, q, k, v, dim_mask, attn_mask, dropout_mask, alibi_mask, kernel_name,
                 tiling_stgy_cls,
                 prev_block_num=65536,
                 next_block_num=65536,
                 high_precision=False,
                 disable_debug=True):
        """
        Init parameter shape
        :param q: with shape: (B, h, N, d)
        :param k: with shape: (B, h, N, d)
        :param v: with shape: (B, h, N, d)
        :param dim_mask: with shape: (x, ) x equals length last dim that not padded to 16.
        :param attn_mask: with shape: (1, N, N) or (B, N, N)
        :param dropout_mask: with shape: (B, h, N, N)
        :param alibi_mask: with shape: (B, h, 1, N)
        :param kernel_name:
        :param tiling_stgy_cls:
        :param prev_block_num:
        :param next_block_num:
        :param disable_debug:
        """
        self.tik_instance = tik.Tik(disable_debug=disable_debug)
        self.core_num = get_soc_spec(tbe_platform.CORE_NUM)
        self.M = tbe_platform.get_soc_spec(tbe_platform.L1_SIZE)
        self.kernel_name = kernel_name
        self.cont_data_mv_1_bust = partial(self.tik_instance.data_move, sid=0, nburst=1,
                                           src_stride=0,
                                           dst_stride=0)
        self.tik_ops_utils = TikOpsUtils(self.tik_instance)
        self.parser_input_shape(alibi_mask, attn_mask, dim_mask, dropout_mask, k, q, v)

        batch_size, h, Nq, d = self.q_shape
        self.head_num = h
        self.B, self.Nq, self.d = batch_size * h, Nq, d
        self.N = self.k_shape[2]

        self.l_shape = [batch_size, h, self.Nq]
        self.m_shape = [batch_size, h, self.Nq]
        self.O_shape = [batch_size, h, self.Nq, self.d]
        self.actual_d = self.dim_mask_shape[0]

        self.K0 = 16
        self.prev_block_num = prev_block_num
        self.next_block_num = next_block_num
        self.high_precision = high_precision
        if self.high_precision:
            self.precision_type = FP32
        else:
            self.precision_type = FP16

        if tiling_stgy_cls is None:
            self.tiling_stgy = XunfeiTiling(self.Nq, self.N, self.d)
        else:
            self.tiling_stgy: TilingStrategy = tiling_stgy_cls(self.Nq, self.N, self.d)
        self.Br = None
        self.last_Br = None
        self.Bc = None
        self.last_Bc = None
        self.Tr = None
        self.Tc = None
        self.Q_gm = None
        self.K_gm = None
        self.V_gm = None
        self.dim_mask_gm = None
        self.att_mask_gm = None
        self.drop_mask_gm = None
        self.alibi_mask_gm = None

    @staticmethod
    def get_gm_offset(batch_start, batch_idx, h, w, block_h, block_idx):
        """get gm offset"""
        gm_offset = (batch_start + batch_idx) * h * w + block_idx * block_h * w
        return gm_offset

    @staticmethod
    def get_alibi_gm_offset(batch_start, batch_idx, w, block_w, block_idx):
        """get alibi gm offset"""
        gm_offset = (batch_start + batch_idx) * w + block_idx * block_w
        return gm_offset

    @staticmethod
    def get_drop_mask_gm_offset(batch_start, batch_idx, h, w, block_h, block_h_idx, block_w, block_w_idx):
        """get drop mask gm offset"""
        gm_offset = (batch_start + batch_idx) * h * w + block_h_idx * (w * block_h) + block_w_idx * block_w
        return gm_offset

    @abstractmethod
    def define_custom_inputs(self):
        """define custom inputs"""
        raise NotImplementedError

    @abstractmethod
    def define_outputs(self):
        """define outputs"""
        raise NotImplementedError

    @abstractmethod
    def collect_inputs(self):
        """collect inputs"""
        raise NotImplementedError

    @abstractmethod
    def collect_outputs(self):
        """collect outputs"""
        raise NotImplementedError

    def get_core_bath_info(self):
        """
        Get batch start and batch number of each NPU core.
        :return: Tensor([[core_1_batch_start, core_1_batch_num],...,[core_m_batch_start,
        core_m_batch_num]]), Tensor([[[core_1_batch_1_Tr_start, core_1_batch_1_Tr_end],...[core_1_batch_n_Tr_start,
        core_1_batch_n_Tr_end]],...,[[core_m_batch_1_Tr_start, core_m_batch_1_Tr_end],...[core_m_batch_n_Tr_start,
        core_m_batch_n_Tr_end]]
        """
        if self.core_num > self.B * self.Tr:
            self.core_num = self.B * self.Tr

        task_idx_to_batch_tr_idx = dict()
        for task_idx in range(self.B * self.Tr):
            batch_idx = task_idx // self.Tr
            tr_idx = task_idx % self.Tr
            task_idx_to_batch_tr_idx[task_idx] = [batch_idx, tr_idx]

        core_idx_to_batch_idx = defaultdict(lambda: [100000, -1])
        core_idx_to_tr_idx = defaultdict(lambda: defaultdict(lambda: [100000, -1]))
        task_start = 0
        avg_task_num_per_core, remain_task = divmod(self.B * self.Tr, self.core_num)

        for core_idx in range(self.core_num):
            cur_core_task_num = avg_task_num_per_core
            if core_idx < remain_task:
                cur_core_task_num += 1
            task_end = task_start + cur_core_task_num
            for task_idx in range(task_start, task_end):
                try:
                    batch_idx, tr_idx = task_idx_to_batch_tr_idx[task_idx]
                except KeyError:
                    raise ValueError("The argument 'task_idx' is not valid.")
                batch_start_end_pair = core_idx_to_batch_idx[core_idx]
                if batch_idx < batch_start_end_pair[0]:
                    batch_start_end_pair[0] = batch_idx
                if batch_idx > batch_start_end_pair[1]:
                    batch_start_end_pair[1] = batch_idx
                tr_start_end_pair = core_idx_to_tr_idx[core_idx][batch_idx]
                if tr_idx < tr_start_end_pair[0]:
                    tr_start_end_pair[0] = tr_idx
                if tr_idx > tr_start_end_pair[1]:
                    tr_start_end_pair[1] = tr_idx
            task_start = task_end

        core_idx_to_batch_info = self.tik_instance.Tensor(
            "int32", (self.core_num, 2), name="core_idx_to_batch_info", scope=UB
        )
        core_idx_to_tr_info = self.tik_instance.Tensor(
            "int32", (self.core_num, self.B, 2), name="core_idx_to_tr_info", scope=UB
        )
        for core_idx in core_idx_to_batch_idx:
            batch_start, batch_end = core_idx_to_batch_idx[core_idx]
            core_idx_to_batch_info[core_idx, 0] = batch_start
            core_idx_to_batch_info[core_idx, 1] = batch_end - batch_start + 1
            for batch_idx in core_idx_to_tr_idx[core_idx]:
                tr_start, tr_end = core_idx_to_tr_idx[core_idx][batch_idx]
                core_idx_to_tr_info[core_idx, batch_idx, 0] = tr_start
                core_idx_to_tr_info[core_idx, batch_idx, 1] = tr_end + 1
        return core_idx_to_batch_info, core_idx_to_tr_info

    def get_attn_mask_gm_offset(self, batch_start, batch_idx, h, w, block_h, block_h_idx, block_w, block_w_idx):
        """get attn mask gm offset"""
        if self.att_mask_shape[0] == 1:
            gm_offset = block_h_idx * (w * block_h) + block_w_idx * block_w
        else:
            gm_offset = ((batch_start + batch_idx) // self.head_num) * h * w \
                        + block_h_idx * (w * block_h) + block_w_idx * block_w
        return gm_offset

    def parser_input_shape(self, alibi_mask, attn_mask, dim_mask, dropout_mask, k, q, v):
        """parser input shape"""
        self.has_attn_mask = False
        self.has_drop_mask = False
        self.has_alibi_mask = False
        if isinstance(q, dict):
            self.q_shape = q["shape"]
            self.k_shape = k["shape"]
            self.v_shape = v["shape"]
            self.dim_mask_shape = dim_mask["shape"]
            if attn_mask is not None:
                self.has_attn_mask = True
                self.att_mask_shape = attn_mask["shape"]
            if dropout_mask is not None:
                self.has_drop_mask = True
                self.drop_mask_shape = dropout_mask["shape"]
            if alibi_mask is not None:
                self.has_alibi_mask = True
                self.alibi_mask_shape = alibi_mask["shape"]
        else:
            self.q_shape = q.shape
            self.k_shape = k.shape
            self.v_shape = v.shape
            self.dim_mask_shape = dim_mask.shape
            if attn_mask is not None:
                self.has_attn_mask = True
                self.att_mask_shape = attn_mask.shape
            if dropout_mask is not None:
                self.has_drop_mask = True
                self.drop_mask_shape = dropout_mask.shape
            if alibi_mask is not None:
                self.has_alibi_mask = True
                self.alibi_mask_shape = alibi_mask.shape

    def define_inputs_outputs(self):
        """define inputs outputs"""
        self.define_common_inputs()

        self.define_custom_inputs()

        self.define_outputs()

    def init(self):
        """init parameters"""
        tiling_para: TilingPara = self.tiling_stgy.tiling()

        self.Br = tiling_para.Br
        self.last_Br = tiling_para.last_Br
        self.Bc = tiling_para.Bc
        self.last_Bc = tiling_para.last_Bc
        self.Tr = tiling_para.Tr
        self.Tc = tiling_para.Tc

        self.define_inputs_outputs()

    def define_common_inputs(self):
        """define common input gm tensors"""
        self.Q_gm = self.tik_instance.Tensor(FP16, self.q_shape, name="Q_gm", scope=GM)
        self.K_gm = self.tik_instance.Tensor(FP16, self.k_shape, name="K_gm", scope=GM)
        self.V_gm = self.tik_instance.Tensor(FP16, self.v_shape, name="V_gm", scope=GM)
        self.dim_mask_gm = self.tik_instance.Tensor(INT8, self.dim_mask_shape, name="mask_gm",
                                                    scope=GM)
        if self.has_attn_mask:
            self.att_mask_gm = self.tik_instance.Tensor(FP16, self.att_mask_shape,
                                                        name="att_mask_gm", scope=GM)
        if self.has_drop_mask:
            self.drop_mask_gm = self.tik_instance.Tensor(FP16, self.drop_mask_shape,
                                                         name="drop_mask_gm", scope=GM)
        if self.has_alibi_mask:
            self.alibi_mask_gm = self.tik_instance.Tensor(FP16, self.alibi_mask_shape,
                                                          name="alibi_mask_gm", scope=GM)

    def do_alibi_mask(self, Sij_ub, alibi_mask_gm_offset, m_aligned, n_aligned):
        """load alibi mask from gm to ub, then add Sij"""
        with self.tik_instance.new_stmt_scope(disable_sync=False):
            alibi_mask_ub = self.tik_instance.Tensor(FP16, (1, n_aligned),
                                                     scope=UB, name="alibi_mask_ub")
            self.tik_instance.data_move(alibi_mask_ub, self.alibi_mask_gm[alibi_mask_gm_offset], 0, 1,
                                        n_aligned // 16, 0, 0)
            alibi_mask_ub_broadcast = self.tik_ops_utils.broadcast_row(alibi_mask_ub, (m_aligned, n_aligned))
            self.tik_instance.h_add(Sij_ub, Sij_ub, alibi_mask_ub_broadcast)

    def do_att_mask(self, Sij_ub, attn_mask_gm_offset, q_blk_height, kv_blk_height,
                    q_blk_h_aligned, kv_blk_h_aligned):
        """load attn mask from gm to ub, then mul it by MASK_FILL_VALUE and add Sij"""
        with self.tik_instance.new_stmt_scope(disable_sync=False):
            att_mask_ub = self.tik_instance.Tensor(FP16, (q_blk_h_aligned, kv_blk_h_aligned),
                                                   scope=UB, name="att_mask_ub")
            self.tik_instance.data_move(att_mask_ub, self.att_mask_gm[attn_mask_gm_offset], 0,
                                        q_blk_height, kv_blk_height // 16, (self.N - kv_blk_height) // 16, 0)
            self.tik_instance.h_mul(att_mask_ub, att_mask_ub, MASK_FILL_VALUE)
            self.tik_instance.h_add(Sij_ub, Sij_ub, att_mask_ub)

    def do_dropout_mask(self, Pij_ub, dropout_mask_gm_offset, kv_blk_h_aligned, kv_blk_height,
                        q_blk_h_aligned, q_blk_height, precision_type=FP16):
        """load drop mask from gm to ub, then mul it by Pij"""
        with self.tik_instance.new_stmt_scope(disable_sync=False):
            dropout_mask_ub = self.tik_instance.Tensor(FP16, (q_blk_h_aligned, kv_blk_h_aligned),
                                                       scope=UB, name="drop_mask_ub")
            self.tik_instance.data_move(dropout_mask_ub, self.drop_mask_gm[dropout_mask_gm_offset], 0,
                                        q_blk_height, kv_blk_height // 16, (self.N - kv_blk_height) // 16, 0)
            if precision_type == FP32:
                dropout_mask_ub_fp32 = self.tik_instance.Tensor(FP32, (q_blk_h_aligned, kv_blk_h_aligned),
                                                                scope=UB, name="dropout_mask_ub_fp32")
                self.tik_instance.h_cast(dropout_mask_ub_fp32, dropout_mask_ub, "none")
                self.tik_instance.h_mul(Pij_ub, Pij_ub, dropout_mask_ub_fp32)
            else:
                self.tik_instance.h_mul(Pij_ub, Pij_ub, dropout_mask_ub)

    @abstractmethod
    def compute_one_core(self, batch_start_s, batch_num_s, core_idx_to_tr_info, core_idx):
        """compute one core"""
        raise NotImplementedError

    def compute_process(self):
        """The compute process of FlashAttention"""
        self.init()

        core_idx_to_batch_info, core_idx_to_tr_info = self.get_core_bath_info()
        with self.tik_instance.for_range(begint=0, endt=self.core_num, name="core_index",
                                         block_num=self.core_num) as core_idx:
            batch_start_s = self.tik_instance.Scalar("int32", name="batch_start_s")
            batch_num_s = self.tik_instance.Scalar("int32", name="batch_num_s")

            batch_start_s.set_as(core_idx_to_batch_info[core_idx, 0])
            batch_num_s.set_as(core_idx_to_batch_info[core_idx, 1])

            self.compute_one_core(batch_start_s, batch_num_s, core_idx_to_tr_info, core_idx)

        self.tik_instance.BuildCCE(
            kernel_name=self.kernel_name,
            inputs=self.collect_inputs(),
            outputs=self.collect_outputs(),
            config={"dump_cce_code": False, "save_temp_cce_file": True, "enable_const_fold": True},
            enable_l2=True
        )
