/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MINDSPORE_LITE_SRC_RUNTIME_KERNEL_CPU_FP16_LSTM_FP16_H_
#define MINDSPORE_LITE_SRC_RUNTIME_KERNEL_CPU_FP16_LSTM_FP16_H_

#include <vector>
#include "src/litert/lite_kernel.h"
#include "nnacl/lstm_parameter.h"

namespace mindspore::kernel {
/*
 * 1. LSTM(exclude mindir) without project
 *    weight_ih: second input, shape is [bidirectional, 4 * hidden_size, input_size]
 *    weight_hh: third input, shape is [bidirectional, 4 * hidden_size, hidden_size]
 *    bias: forth input, shape is [bidirectional, 8 * hidden_size]
 *    h_init: fifth input, shape is [bidirectional, batch_size, hidden_size]
 *    c_init: sixth input, shape is [bidirectional, batch_size, hidden_size]
 *
 * 2. LSTM(exclude mindir) with project
 *    weight_ih: second input, shape is [bidirectional, 4 * hidden_size, input_size]
 *    weight_hh: third input, shape is [bidirectional, 4 * hidden_size, project_size]
 *    bias: forth input, shape is [bidirectional, 8 * hidden_size]
 *    h_init: fifth input, shape is [bidirectional, batch_size, project_size]
 *    c_init: sixth input, shape is [bidirectional, batch_size, hidden_size]
 *    weight_pro: seventh input, shape is [bidirectional, project_size, hidden_size]
 */
class LstmFp16CPUKernel : public LiteKernel {
 public:
  LstmFp16CPUKernel(OpParameter *parameter, const std::vector<lite::Tensor *> &inputs,
                    const std::vector<lite::Tensor *> &outputs, const lite::InnerContext *ctx)
      : LiteKernel(parameter, inputs, outputs, ctx) {
    lstm_param_ = reinterpret_cast<LstmParameter *>(op_parameter_);
  }

  ~LstmFp16CPUKernel() override { FreeTmpBuffer(); }

  int Prepare() override;
  int ReSize() override;
  int Run() override;

 private:
  void FreeTmpBuffer();
  void FreeRunBuffer();
  int InitParam();
  int InitInputWeightBias();
  int InitStateWeightBias();
  int InitProjectWeight();
  int MallocRunBuffer();

  float16_t *weight_i_ptr_ = nullptr;
  float16_t *weight_h_ptr_ = nullptr;
  float16_t *weight_project_ptr_ = nullptr;
  float16_t *input_bias_ = nullptr;
  float16_t *state_bias_ = nullptr;
  float16_t *project_bias_ = nullptr;

  float16_t *buffer_[C7NUM] = {0};
  const int gate_num = 4;
  const int packed_input_index = 0;
  const int input_gate_index = 1;
  const int packed_state_index = 2;
  const int state_gate_index = 3;
  const int cell_state_index = 4;
  const int hidden_state_index = 5;
  const int project_input_index = 6;

  int weight_batch_ = 0;
  bool is_vec_ = false;
  LstmParameter *lstm_param_ = nullptr;
};
}  // namespace mindspore::kernel

#endif  // MINDSPORE_LITE_SRC_RUNTIME_KERNEL_CPU_FP16_LSTM_FP16_H_
