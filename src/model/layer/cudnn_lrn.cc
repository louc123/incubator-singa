/*********************************************************
*
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  See the NOTICE file
* distributed with this work for additional information
* regarding copyright ownership.  The ASF licenses this file
* to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an
* "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
* KIND, either express or implied.  See the License for the
* specific language governing permissions and limitations
* under the License.
*
************************************************************/
#include "cudnn_lrn.h"
#ifdef USE_CUDNN
#include "cudnn_utils.h"

namespace singa {
CudnnLRN::~CudnnLRN() {
  if (has_init_cudnn_) {
    CUDNN_CHECK(cudnnDestroyLRNDescriptor(lrn_desc_));
    CUDNN_CHECK(cudnnDestroyTensorDescriptor(shape_desc_));
  }
}
void CudnnLRN::InitCudnn(const Shape& shape , DataType dtype) {
  CHECK(!has_init_cudnn_);
  mode_ = CUDNN_LRN_CROSS_CHANNEL_DIM1;
  CUDNN_CHECK(cudnnCreateTensorDescriptor(&shape_desc_));
  CHECK_EQ(shape.size(), 4);
  CUDNN_CHECK(cudnnSetTensor4dDescriptor(shape_desc_,
      CUDNN_TENSOR_NCHW,
      GetCudnnDataType(dtype),
      shape[0],
      shape[1],
      shape[2],
      shape[3]));
  CUDNN_CHECK(cudnnCreateLRNDescriptor(&lrn_desc_));
  CUDNN_CHECK(cudnnSetLRNDescriptor(lrn_desc_,
        local_size_,
        alpha_,
        beta_,
        k_));
  has_init_cudnn_ = true;
}
const Tensor CudnnLRN::Forward(int flag, const Tensor& input) {
  auto shape = input.shape();
  auto dtype = input.data_type();
  if (!has_init_cudnn_)
    InitCudnn(shape, dtype);
  Tensor output;
  output.ResetLike(input);
  output.device()->Exec(
      [=](Context* ctx) {
        Blob *inblob = input.blob(), *outblob = output.blob();
        const float alpha = 1.0f, beta = 0.0f;
        CUDNN_CHECK(cudnnLRNCrossChannelForward(ctx->cudnn_handle,
            this->lrn_desc_,
            this->mode_,
            &alpha,
            this->shape_desc_,
            inblob->data(),
            &beta,
            this->shape_desc_,
            outblob->mutable_data()));
      }, {input.blob()}, {output.blob()});
  buf_.push(input);
  buf_.push(output);
  return output;
}

const std::pair<Tensor, vector<Tensor>> CudnnLRN::Backward(
    int flag, const Tensor& grad) {
  vector <Tensor> param_grad;
  Tensor dx;
  Tensor output = buf_.top();
  buf_.pop();
  Tensor input = buf_.top();
  buf_.pop();
  if ((flag & kTrain) == kTrain) {
    dx.ResetLike(grad);
    dx.device()->Exec(
        [=](Context *ctx) {
          Blob *dyblob = grad.blob(), *dxblob = dx.blob();
          Blob *yblob = output.blob(), *xblob = input.blob();
          float alpha = 1.0f, beta = 0.0f;
          CUDNN_CHECK(cudnnLRNCrossChannelBackward(ctx->cudnn_handle,
              this->lrn_desc_,
              this->mode_,
              &alpha,
              this->shape_desc_,
              yblob->data(),
              this->shape_desc_,
              dyblob->data(),
              this->shape_desc_,
              xblob->data(),
              &beta,
              this->shape_desc_,
              dxblob->mutable_data()));
        }, {output.blob(), grad.blob(), input.blob()}, {dx.blob()});
  } else {
    LOG(ERROR) << "Do not call backward for evaluation phase";
  }
  return std::make_pair(dx, param_grad);
}
}  // namespace

#endif  // USE_CUDNN
