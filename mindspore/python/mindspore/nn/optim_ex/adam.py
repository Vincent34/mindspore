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
"""adam"""
from __future__ import absolute_import

from mindspore.ops import functional as F, operations as P
from mindspore.common.parameter import Parameter, ParameterTuple
from mindspore.common.tensor import Tensor
import mindspore.common.dtype as mstype
from mindspore.nn.optim_ex.optimizer import Optimizer
from mindspore import ops


class Adam(Optimizer):
    r"""
    Implements Adam algorithm..

    The updating formulas are as follows:

    .. math::
        \begin{aligned}
            &\textbf{input}      : \gamma \text{ (lr)}, \beta_1, \beta_2
                \text{ (betas)},\theta_0 \text{ (params)},f(\theta) \text{ (objective)}          \\
            &\hspace{13mm}      \lambda \text{ (weight decay)},  \: \textit{amsgrad},
                \:\textit{maximize}                                                              \\
            &\textbf{initialize} :  m_0 \leftarrow 0 \text{ ( first moment)},
                v_0\leftarrow 0 \text{ (second moment)},\: \widehat{v_0}^{max}\leftarrow 0\\[-1.ex]
            &\textbf{for} \: t=1 \: \textbf{to} \: \ldots \: \textbf{do}                         \\
            &\hspace{5mm}\textbf{if} \: \textit{maximize}:                                       \\
            &\hspace{10mm}g_t           \leftarrow   -\nabla_{\theta} f_t (\theta_{t-1})         \\
            &\hspace{5mm}\textbf{else}                                                           \\
            &\hspace{10mm}g_t           \leftarrow   \nabla_{\theta} f_t (\theta_{t-1})          \\
            &\hspace{5mm}\textbf{if} \: \lambda \neq 0                                           \\
            &\hspace{10mm} g_t \leftarrow g_t + \lambda  \theta_{t-1}                            \\
            &\hspace{5mm}m_t           \leftarrow   \beta_1 m_{t-1} + (1 - \beta_1) g_t          \\
            &\hspace{5mm}v_t           \leftarrow   \beta_2 v_{t-1} + (1-\beta_2) g^2_t          \\
            &\hspace{5mm}\widehat{m_t} \leftarrow   m_t/\big(1-\beta_1^t \big)                   \\
            &\hspace{5mm}\widehat{v_t} \leftarrow   v_t/\big(1-\beta_2^t \big)                   \\
            &\hspace{5mm}\textbf{if} \: amsgrad                                                  \\
            &\hspace{10mm}\widehat{v_t}^{max} \leftarrow \mathrm{max}(\widehat{v_t}^{max},
                \widehat{v_t})                                                                   \\
            &\hspace{10mm}\theta_t \leftarrow \theta_{t-1} - \gamma \widehat{m_t}/
                \big(\sqrt{\widehat{v_t}^{max}} + \epsilon \big)                                 \\
            &\hspace{5mm}\textbf{else}                                                           \\
            &\hspace{10mm}\theta_t \leftarrow \theta_{t-1} - \gamma \widehat{m_t}/
                \big(\sqrt{\widehat{v_t}} + \epsilon \big)                                       \\
            &\bf{return} \:  \theta_t                                                     \\[-1.ex]
       \end{aligned}

    .. warning::
        This is an experimental optimizer API that is subject to change.
        This module must be used with lr scheduler module in `LRScheduler Class
        <https://www.mindspore.cn/docs/en/master/api_python/mindspore.nn.html#lrscheduler>`_ .

    Args:
        params (Union[list(Parameter), list(dict)]): list of parameters to optimize or dicts defining
            parameter groups
        lr (Union[int, float, Tensor], optional): learning rate. Default: ``1e-3``.
        betas (Tuple[float, float], optional): The exponential decay rate for the moment estimations.
            Default: ``(0.9, 0.999)``.
        eps (float, optional): term added to the denominator to improve
            numerical stability. Default: ``1e-8``.
        weight_decay (float, optional): weight decay (L2 penalty). Default: ``0``.
        amsgrad (bool, optional): whether to use the AMSGrad algorithm. Default: ``False``.

    Keyword Args:
        maximize (bool, optional): maximize the params based on the objective, instead of minimizing.
            Default: ``False``.

    Inputs:
        - **gradients** (tuple[Tensor]) - The gradients of `params`.

    Raises:
        ValueError: If the `lr` is not int, float or Tensor.
        ValueError: If the `lr` is less than 0.
        ValueError: If the `eps` is less than 0.0.
        ValueError: If the `betas` not in the range of 0-1.
        ValueError: If the `weight_decay` is less than 0.

    Supported Platforms:
        ``Ascend`` ``GPU`` ``CPU``

    Examples:
        >>> import mindspore
        >>> from mindspore import nn
        >>> # Define the network structure of LeNet5. Refer to
        >>> # https://gitee.com/mindspore/docs/blob/master/docs/mindspore/code/lenet.py
        >>> net = LeNet5()
        >>> loss_fn = nn.SoftmaxCrossEntropyWithLogits(sparse=True)
        >>> optimizer = nn.optim_ex.Adam(net.trainable_params(), lr=0.1)
        >>> def forward_fn(data, label):
        ...     logits = net(data)
        ...     loss = loss_fn(logits, label)
        ...     return loss, logits
        >>> grad_fn = mindspore.value_and_grad(forward_fn, None, optimizer.parameters, has_aux=True)
        >>> def train_step(data, label):
        ...     (loss, _), grads = grad_fn(data, label)
        ...     optimizer(grads)
        ...     return loss
    """
    def __init__(self, params, lr=1e-3, betas=(0.9, 0.999), eps=1e-8,
                 weight_decay=0, amsgrad=False, *, maximize=False):
        if lr < 0.0:
            raise ValueError("Invalid learning rate: {}".format(lr))
        if eps < 0.0:
            raise ValueError("Invalid epsilon value: {}".format(eps))
        if not 0.0 <= betas[0] < 1.0:
            raise ValueError("Invalid beta parameter at index 0: {}".format(betas[0]))
        if not 0.0 <= betas[1] < 1.0:
            raise ValueError("Invalid beta parameter at index 1: {}".format(betas[1]))
        if weight_decay < 0.0:
            raise ValueError("Invalid weight_decay value: {}".format(weight_decay))

        defaults = dict(lr=lr, betas=betas, eps=eps,
                        weight_decay=weight_decay, amsgrad=amsgrad,
                        maximize=maximize)
        super(Adam, self).__init__(params, defaults)

        self.exp_avg = self.parameters.clone(prefix="exp_avg", init='zeros')
        self.exp_avg_sq = self.parameters.clone(prefix="exp_avg_sq", init='zeros')
        self.max_exp_avg_sq = self.parameters.clone(prefix="max_exp_avg_sq", init='zeros')
        self.state_step = ParameterTuple(Parameter(
            Tensor(0, mstype.int32), "step_"+str(i)) for i in range(len(self.parameters)))
        self.increase_tensor = Tensor(1, mstype.int32)
        self.op_mul = P.Mul()
        self.assignadd = P.AssignAdd()
        self.op_pow = P.Pow()
        self.op_sqrt = P.Sqrt()
        self.op_maximum = P.Maximum()

    def construct(self, gradients):
        for group_id, group in enumerate(self.param_groups):
            params = []
            grads = []
            exp_avgs = []
            exp_avg_sqs = []
            max_exp_avg_sqs = []
            state_steps = []
            params, grads, exp_avgs, exp_avg_sqs, max_exp_avg_sqs, state_steps = \
                self._init_group(group, gradients, params, grads, exp_avgs, exp_avg_sqs,
                                 max_exp_avg_sqs, state_steps, group_id)
            beta1, beta2 = group['betas']
            self.apply_adam(params, grads, exp_avgs, exp_avg_sqs, max_exp_avg_sqs, state_steps,
                            group['amsgrad'], beta1, beta2, group['lr'], group['weight_decay'], group['eps'],
                            group["maximize"], group["grad_centralization"])

    def apply_adam(self, params, grads, exp_avgs, exp_avg_sqs, max_exp_avg_sqs, state_steps,
                   amsgrad, beta1, beta2, lr, weight_decay, eps, maximize, grad_centralization):
        grads = self._decay_weight(weight_decay, params, grads)
        grads = self._gradients_centralization(grad_centralization, grads)

        for i, param in enumerate(params):
            grad = grads[i] if not maximize else -grads[i]
            exp_avg = exp_avgs[i]
            exp_avg_sq = exp_avg_sqs[i]
            step_t = state_steps[i]

            F.assign(exp_avg, self.op_mul(exp_avg, beta1) + self.op_mul(grad, 1-beta1))
            F.assign(exp_avg_sq, ops.addcmul(self.op_mul(exp_avg_sq, beta2), grad, grad.conj(), 1-beta2))
            step_t = F.depend(step_t, self.assignadd(step_t, self.increase_tensor))

            bias_correction1 = F.tuple_to_array((1.0,)) - self.op_pow(beta1, step_t)
            bias_correction2 = F.tuple_to_array((1.0,)) - self.op_pow(beta2, step_t)
            step_size = lr / bias_correction1
            bias_correction2_sqrt = self.op_sqrt(bias_correction2)

            if amsgrad:
                F.assign(max_exp_avg_sqs[i], self.op_maximum(max_exp_avg_sqs[i], exp_avg_sq))
                denom = self.op_sqrt(max_exp_avg_sqs[i]) / bias_correction2_sqrt + eps
            else:
                denom = self.op_sqrt(exp_avg_sq) / bias_correction2_sqrt + eps
            param = F.depend(param, denom)
            next_param = param - self.op_mul(exp_avg / denom, step_size)
            F.assign(param, next_param)

    def _init_group(self, group, gradients, params, grads, exp_avgs, exp_avg_sqs,
                    max_exp_avg_sqs, state_steps, group_id):
        """ Initialize group params. """
        p_id = self.group_start_id[group_id]
        for i, param in enumerate(group["params"]):
            params.append(param)
            grads.append(gradients[p_id+i])
            exp_avgs.append(self.exp_avg[p_id+i])
            exp_avg_sqs.append(self.exp_avg_sq[p_id+i])
            max_exp_avg_sqs.append(self.max_exp_avg_sq[p_id+i])
            state_steps.append(self.state_step[p_id+i])
        return params, grads, exp_avgs, exp_avg_sqs, max_exp_avg_sqs, state_steps
