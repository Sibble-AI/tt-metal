// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

#define DEBUG_PRINT 0
#define RUN_COMPUTE 0

void kernel_main() {
    std::uint32_t l1_buffer_addr        = get_arg_val<uint32_t>(0);

    std::uint32_t src0_dram_buffer_addr  = get_arg_val<uint32_t>(1);
    std::uint32_t src0_dram_noc_x        = get_arg_val<uint32_t>(2);
    std::uint32_t src0_dram_noc_y        = get_arg_val<uint32_t>(3);

    std::uint32_t src1_dram_buffer_addr  = get_arg_val<uint32_t>(4);
    std::uint32_t src1_dram_noc_x        = get_arg_val<uint32_t>(5);
    std::uint32_t src1_dram_noc_y        = get_arg_val<uint32_t>(6);

    std::uint32_t dst_dram_buffer_addr  = get_arg_val<uint32_t>(7);
    std::uint32_t dst_dram_noc_x        = get_arg_val<uint32_t>(8);
    std::uint32_t dst_dram_noc_y        = get_arg_val<uint32_t>(9);

    std::uint32_t buffer_size           = get_arg_val<uint32_t>(10); //in bytes

    std::uint64_t src0_dram_noc_addr = get_noc_addr(src0_dram_noc_x, src0_dram_noc_y, src0_dram_buffer_addr);
    std::uint64_t src1_dram_noc_addr = get_noc_addr(src1_dram_noc_x, src1_dram_noc_y, src1_dram_buffer_addr);
    std::uint64_t dst_dram_noc_addr = get_noc_addr(dst_dram_noc_x, dst_dram_noc_y, dst_dram_buffer_addr);

    int buffer_elems = buffer_size >> 2; //4 bytes per word
    int num_iters = 1024*64;

    for (int iter = 0; iter < num_iters; iter++){
        int buffer_idx = iter % buffer_elems;
        #if DEBUG_PRINT
            DPRINT << "Buffer size: " << buffer_size << ENDL();
            DPRINT << "Buffer elems: " << buffer_elems << ENDL();
            DPRINT << "Reading src0" << ENDL();
        #endif

        noc_async_read(src0_dram_noc_addr, l1_buffer_addr, buffer_size);
        noc_async_read(src1_dram_noc_addr, l1_buffer_addr + buffer_size, buffer_size);

        #if DEBUG_PRINT
            DPRINT << "Reading src1" << ENDL();
        #endif

        noc_async_read_barrier();

        #if DEBUG_PRINT
            DPRINT << "Doing math" << ENDL();
        #endif

        uint32_t* dat0 = (uint32_t*)l1_buffer_addr;
        uint32_t* dat1 = (uint32_t*)(l1_buffer_addr+buffer_size);

        #if DEBUG_PRINT
            DPRINT << "dat0 before: " << dat0[0] << ENDL();
            DPRINT << "dat1 before: " << dat1[0] << ENDL();
        #endif

        #if RUN_COMPUTE
            for (int i = 0; i < buffer_elems; i++) {
                dat0[i] = dat0[i] + dat1[i];
            }
        #else
            dat0[buffer_idx] = dat0[buffer_idx] + dat1[buffer_idx];
        #endif

        #if DEBUG_PRINT
            DPRINT << "dat0 after: " << dat0[0] << ENDL();
        #endif


        #if DEBUG_PRINT
            DPRINT << "Writing out" << ENDL();
        #endif
    }

    noc_async_write(l1_buffer_addr, dst_dram_noc_addr, buffer_size);
    noc_async_write_barrier();

    #if DEBUG_PRINT
        DPRINT << "Done writing out" << ENDL();
    #endif
}
