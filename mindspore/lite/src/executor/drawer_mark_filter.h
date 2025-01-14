/**
 * Copyright 2023 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_LITE_SRC_EXECUTOR_DRAWER_MARK_FILTER_H_
#define MINDSPORE_LITE_SRC_EXECUTOR_DRAWER_MARK_FILTER_H_

#include <functional>
#include "src/executor/kernel_exec.h"

namespace mindspore::lite {
using MarkFilter = std::function<bool(const kernel::KernelExec &)>;
}  // namespace mindspore::lite

#endif  // MINDSPORE_LITE_SRC_EXECUTOR_DRAWER_MARK_FILTER_H_
