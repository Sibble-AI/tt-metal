// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

#define DEBUG_PRINT 1

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
    std::uint32_t num_tiles           = get_arg_val<uint32_t>(11); //in bytes

    std::uint64_t src0_dram_noc_addr = get_noc_addr(src0_dram_noc_x, src0_dram_noc_y, src0_dram_buffer_addr);
    std::uint64_t src1_dram_noc_addr = get_noc_addr(src1_dram_noc_x, src1_dram_noc_y, src1_dram_buffer_addr);
    std::uint64_t dst_dram_noc_addr = get_noc_addr(dst_dram_noc_x, dst_dram_noc_y, dst_dram_buffer_addr);

    int buffer_elems = buffer_size >> 2; //4 bytes per word
    const int page_size = 4*32*32;

    #if DEBUG_PRINT
        DPRINT << "Buffer size: " << buffer_size << ENDL();
        DPRINT << "Buffer elems: " << buffer_elems << ENDL();
        DPRINT << "Reading src0" << ENDL();
    #endif

    const InterleavedAddrGenFast<true> s0 = {
        .bank_base_address = src0_dram_buffer_addr,
        .page_size = page_size,
        .data_format = DataFormat::UInt32
    };

    const InterleavedAddrGenFast<true> s1 = {
        .bank_base_address = src1_dram_buffer_addr,
        .page_size = page_size,
        .data_format = DataFormat::UInt32
    };


    for (std::uint32_t tile_id = 0; tile_id < num_tiles; tile_id++){
        noc_async_read_tile(tile_id, s0, l1_buffer_addr + tile_id*page_size);
        noc_async_read_barrier();
        noc_async_read_tile(tile_id, s1, l1_buffer_addr + buffer_size + tile_id*page_size);
        noc_async_read_barrier();
    }

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
        DPRINT << "dat0[0] before: " << dat0[0] << ENDL();
        DPRINT << "dat1[0] before: " << dat1[0] << ENDL();
        DPRINT << "dat0[N-1] before: " << dat0[(buffer_elems-1)] << ENDL();
        DPRINT << "dat1[N-1] before: " << dat1[(buffer_elems -1)] << ENDL();
    #endif

    for (int i = 0; i < buffer_elems; i++) {
        dat0[i] = dat0[i] + dat1[i];
    }

    #if DEBUG_PRINT
        DPRINT << "dat0 after: " << dat0[0] << ENDL();
    #endif


    #if DEBUG_PRINT
        DPRINT << "Writing out" << ENDL();
    #endif

    noc_async_write(l1_buffer_addr, dst_dram_noc_addr, buffer_size);
    noc_async_write_barrier();

    #if DEBUG_PRINT
        DPRINT << "Done writing out" << ENDL();
    #endif
}
