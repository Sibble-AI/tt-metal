// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_metal/host_api.hpp"
#include "tt_metal/detail/tt_metal.hpp"
#include "common/bfloat16.hpp"

/*
* 1. Host writes data to buffer in DRAM
* 2. dram_copy kernel on logical core {0, 0} BRISC copies data from buffer
*      in step 1. to buffer in L1 and back to another buffer in DRAM
* 3. Host reads from buffer written to in step 2.
*/

using namespace tt::tt_metal;

void duration_print(auto start) {
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end-start;
    std::string message = "[Sameet] - Duration test: " + std::to_string(elapsed_seconds.count());
    tt::log_info(tt::LogTest, message.c_str());

}

int main(int argc, char **argv) {


    /*
    * Silicon accelerator, program, command queue setup
    */
    Device *device =CreateDevice(0);
    CommandQueue& cq = device->command_queue();
    Program program = CreateProgram();

    /*
    Make Buffers
    */
    const int num_pages = 16;
    int N = 1024*num_pages;
    const int size_dram = 4*N;
    const int page_size_dram_src = 4*32*32; // reducing page size so there is some interleaving
    const int page_size_dram_dest = 4*N;
    const int size_l1 = 8*N;
    const int page_size_l1 = 8*N;

    tt::tt_metal::InterleavedBufferConfig dram_config{
                .device= device,
                .size = size_dram,
                .page_size = page_size_dram_src,
                .buffer_type = tt::tt_metal::BufferType::DRAM
    };
    tt::tt_metal::InterleavedBufferConfig dram_dest_config{
                .device= device,
                .size = size_dram,
                .page_size = page_size_dram_dest,
                .buffer_type = tt::tt_metal::BufferType::DRAM
    };

    tt::tt_metal::InterleavedBufferConfig l1_config{
                .device= device,
                .size = size_l1,
                .page_size = page_size_l1,
                .buffer_type = tt::tt_metal::BufferType::L1
    };

    auto l1_buffer = CreateBuffer(l1_config);

    auto src_0_buffer = CreateBuffer(dram_config);
    auto src_1_buffer = CreateBuffer(dram_config);
    auto dst_dram_buffer = CreateBuffer(dram_dest_config);

    std::vector<uint32_t> input_vec_1, input_vec_2;
    for (int i = 0; i < N; i++) {
        input_vec_1.push_back(27);
        input_vec_2.push_back(16);
    }

    EnqueueWriteBuffer(cq, src_0_buffer, input_vec_1, false);
    EnqueueWriteBuffer(cq, src_1_buffer, input_vec_2, false);

    constexpr CoreCoord core = {0, 0};

    KernelHandle dram_add_kernel_id = CreateKernel(
        program,
        "tt_metal/programming_examples/sameet_memcpy_profiling/kernels/simple_math_chk.cpp",
        core,
        DataMovementConfig{.processor = DataMovementProcessor::RISCV_0, .noc = NOC::RISCV_0_default}
    );


    const std::vector<uint32_t> runtime_args = {
        l1_buffer->address(),
        src_0_buffer->address(),
        static_cast<uint32_t>(src_0_buffer->noc_coordinates().x),
        static_cast<uint32_t>(src_0_buffer->noc_coordinates().y),
        src_1_buffer->address(),
        static_cast<uint32_t>(src_1_buffer->noc_coordinates().x),
        static_cast<uint32_t>(src_1_buffer->noc_coordinates().y),
        dst_dram_buffer->address(),
        static_cast<uint32_t>(dst_dram_buffer->noc_coordinates().x),
        static_cast<uint32_t>(dst_dram_buffer->noc_coordinates().y),
        4*N,
        num_pages
    };

    SetRuntimeArgs(
        program,
        dram_add_kernel_id,
        core,
        runtime_args
    );

    //run kernel
    auto start = std::chrono::system_clock::now();
    EnqueueProgram(cq, program, false);
    Finish(cq);
    duration_print(start);

    //read output
    std::vector<uint32_t> result_vec;
    EnqueueReadBuffer(cq, dst_dram_buffer, result_vec, true);
    printf("result first element: %d\n", result_vec[0]);
    printf("result last element: %d\n", result_vec[N-1]);

    assert(CloseDevice(device));

}
