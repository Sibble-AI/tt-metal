// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "matmul.hpp"

#include "ttnn/experimental/tt_dnn/op_library/transpose/transpose_op.hpp"
#include "ttnn/operations/core/core.hpp"
#include "ttnn/operations/eltwise/binary/binary.hpp"

namespace ttnn {

using MatmulMultiCoreReuseProgramConfig = tt::operations::primary::MatmulMultiCoreReuseProgramConfig;
using MatmulMultiCoreReuseMultiCastProgramConfig = tt::operations::primary::MatmulMultiCoreReuseMultiCastProgramConfig;
using MatmulMultiCoreReuseMultiCast1DProgramConfig =
    tt::operations::primary::MatmulMultiCoreReuseMultiCast1DProgramConfig;
// MatmulProgramConfig is the Union of the above types
using MatmulProgramConfig = tt::operations::primary::MatmulProgramConfig;

namespace operations {
namespace matmul {

namespace detail {

bool is_input_batched(const ttnn::Shape& shape) {
    auto is_batched = false;
    for (auto i = 0; i < shape.rank() - 2; ++i) {
        if (shape[i] > 1) {
            is_batched = true;
            break;
        }
    }
    return is_batched;
}

}  // namespace detail


std::optional<UnaryWithParam> get_fused_activation(const std::optional<const std::string>& activation) {
    if (!activation.has_value()) {
        return std::nullopt;
    }
    return ttnn::operations::unary::string_to_unary_with_param(activation.value());
}

ttnn::Tensor matmul(
    const ttnn::Tensor& input_tensor_a,
    const ttnn::Tensor& input_tensor_b,
    const std::optional<const ttnn::Tensor>& bias,
    const struct tt::operations::primary::Matmul& parameters) {
    const auto& input_tensor_a_adjusted = parameters.transpose_a ? tt::tt_metal::transpose(input_tensor_a, -1, -2, input_tensor_a.memory_config()) : input_tensor_a;
    const auto& input_tensor_b_adjusted = parameters.transpose_b ? tt::tt_metal::transpose(input_tensor_b, -1, -2, input_tensor_b.memory_config()) : input_tensor_b;

    const auto input_tensor_a_shape = input_tensor_a_adjusted.get_shape();
    const auto input_tensor_b_shape = input_tensor_b_adjusted.get_shape();

    const auto width_a = input_tensor_a_shape[-1];
    const auto height_b = input_tensor_b_shape[-2];

    if (width_a != height_b) {
        TT_THROW("ttnn.matmul: The width of the first tensor must be equal to the height of the second tensor");
    }

    const bool has_program_config = parameters.program_config.has_value();
    const bool has_user_grid = parameters.user_core_coord.has_value();
    bool post_process_bias = false;
    if (bias.has_value()) {
        if (!has_program_config && !has_user_grid) {
            post_process_bias = true;
        }
    }

    auto output_tensor = tt::operations::primary::matmul(
        input_tensor_a_adjusted,
        input_tensor_b_adjusted,
        post_process_bias ? std::nullopt : bias,
        parameters);

    if (post_process_bias) {
        output_tensor = ttnn::add(
            output_tensor, bias.value(), std::nullopt, parameters.output_mem_config);
    }

    if (parameters.user_fused_activation.has_value() && !has_user_grid) {
        const UnaryOpType& op_type = parameters.user_fused_activation.value().op_type;
        if (op_type == UnaryOpType::RELU) {
            output_tensor = ttnn::relu(output_tensor, parameters.output_mem_config);
        } else if (op_type == UnaryOpType::GELU) {
            output_tensor = ttnn::gelu(output_tensor, false, parameters.output_mem_config);
        } else if (op_type == UnaryOpType::SILU) {
            output_tensor = ttnn::silu(output_tensor, parameters.output_mem_config);
        } else {
            TT_THROW("ttnn.matmul: Unsupported activation function");
        }
    }

    return output_tensor;
}

}  // namespace matmul
}  // namespace operations
}  // namespace ttnn
