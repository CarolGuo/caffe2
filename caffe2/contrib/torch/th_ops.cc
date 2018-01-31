/**
 * Copyright (c) 2016-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "caffe2/core/context.h"
#include "caffe2/core/operator.h"

extern "C" {
#include <THNN.h>
}

namespace caffe2 {

namespace {

using UniqueTHFloatTensor =
    std::unique_ptr<THFloatTensor, decltype(&THFloatTensor_free)>;

UniqueTHFloatTensor aliasFromTensorCPU(TensorCPU* tensor) {
  if (!tensor->ndim()) {
    return UniqueTHFloatTensor(THFloatTensor_new(), THFloatTensor_free);
  }

  THLongStorage* thshape = THLongStorage_newWithSize(tensor->ndim());
  for (int i = 0; i < tensor->ndim(); ++i) {
    THLongStorage_set(thshape, i, tensor->dim(i));
  }
  THFloatStorage* storage = THFloatStorage_newWithData(
      tensor->template mutable_data<float>(), tensor->size());
  THFloatStorage_clearFlag(storage, TH_STORAGE_FREEMEM);
  auto* th = THFloatTensor_newWithStorage(storage, 0, thshape, nullptr);
  THFloatStorage_free(storage);
  THLongStorage_free(thshape);
  CAFFE_ENFORCE_EQ(
      THFloatTensor_storage(th)->data, tensor->template mutable_data<float>());
  return UniqueTHFloatTensor(th, THFloatTensor_free);
}

void copyToTensorCPU(UniqueTHFloatTensor th, TensorCPU* tensor) {
  // TODO - if th and tensor point to the same data and have the same
  // size, elide the copy!
  th = UniqueTHFloatTensor(
      THFloatTensor_newContiguous(th.get()), THFloatTensor_free);
  const auto dims = std::vector<TIndex>(
      th->size, th->size + THFloatTensor_nDimension(th.get()));
  // Short-circuit if we never reallocated in TH
  auto* storage = THFloatTensor_storage(th.get());
  // Short-circuit if we never reallocated in TH
  if (dims == tensor->dims() &&
      storage->data == tensor->template data<float>()) {
    THFloatStorage_clearFlag(storage, TH_STORAGE_FREEMEM);
    return;
  }
  tensor->Resize(dims);
  CPUContext ctx;
  ctx.Copy<float, CPUContext, CPUContext>(
      tensor->size(), storage->data, tensor->mutable_data<float>());
}

// _Everything_ below here can be autogenerated with the TBD
// THNN/THCUNN schema. This is just a proof of concept.

class THNNELUCPUOp final : public Operator<CPUContext> {
 public:
  USE_OPERATOR_FUNCTIONS(CPUContext);
  using Operator<CPUContext>::Operator;
  bool RunOnDevice() override {
    // TODO - we can autogenerate this from a schema.
    auto X = aliasFromTensorCPU(const_cast<TensorCPU*>(&Input(0)));
    auto Y = aliasFromTensorCPU(Output(0));
    THNN_FloatELU_updateOutput(
        nullptr,
        X.get(),
        Y.get(),
        GetSingleArgument<float>("alpha", 1.0),
        &Input(0) == Output(0));
    copyToTensorCPU(std::move(Y), Output(0));
    return true;
  }
};

class THNNELUCPUGradientOp final : public Operator<CPUContext> {
 public:
  USE_OPERATOR_FUNCTIONS(CPUContext);
  using Operator<CPUContext>::Operator;

  bool RunOnDevice() override {
    // TODO - we can autogenerate this from a schema.
    auto X = aliasFromTensorCPU(const_cast<TensorCPU*>(&Input(0)));
    auto Y = aliasFromTensorCPU(const_cast<TensorCPU*>(&Input(1)));
    auto dY = aliasFromTensorCPU(const_cast<TensorCPU*>(&Input(2)));
    auto dX = aliasFromTensorCPU(Output(0));
    THNN_FloatELU_updateGradInput(
        nullptr,
        X.get(),
        dY.get(),
        dX.get(),
        Y.get(),
        GetSingleArgument<float>("alpha", 1.0),
        &Input(2) == Output(0) /* inplace */);
    copyToTensorCPU(std::move(dX), Output(0));
    return true;
  }
};

REGISTER_CPU_OPERATOR_WITH_ENGINE(ELU, THNN, THNNELUCPUOp);
REGISTER_CPU_OPERATOR_WITH_ENGINE(ELUGradient, THNN, THNNELUCPUGradientOp);

class GetELUGradient : public GradientMakerBase {
  using GradientMakerBase::GradientMakerBase;
  vector<OperatorDef> GetGradientDefs() override {
    return SingleGradientDef(
        "ELUGradient",
        "",
        vector<string>{I(0), O(0), GO(0)},
        vector<string>{GI(0)},
        Def().arg());
  }
};
REGISTER_GRADIENT(ELU, GetELUGradient);
}
}