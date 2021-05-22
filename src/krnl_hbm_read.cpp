#include "krnl_hbm_read.h"

extern "C" {
void krnl_hbm_read(
    v_dt *d_hbm,       // Data port
    const unsigned int access_offset,
    const unsigned int addr_accum, // target address of writing the accum    
    const unsigned int size,     // Size in integer
    const unsigned int num_times // Running the same kernel operations num_times
) {
// here the gmem0 and gmem34 will not work since the port binding is specificed
// in .cfg for the certain kernel; v++2020.2 should explicitly declare bundle;
// v++2019.2 failed do that (manually multiple copy in kernel code)
#pragma HLS INTERFACE m_axi port = d_hbm offset = slave bundle = gmem0

#pragma HLS INTERFACE s_axilite port = d_hbm
#pragma HLS INTERFACE s_axilite port = access_offset
#pragma HLS INTERFACE s_axilite port = addr_accum
#pragma HLS INTERFACE s_axilite port = size
#pragma HLS INTERFACE s_axilite port = num_times
#pragma HLS INTERFACE s_axilite port = return

  unsigned int v_size = ((size - 1) / VDATA_SIZE) + 1;;
  unsigned int v_access_offset = access_offset / VDATA_SIZE;
  v_dt tmp_read;
  unsigned int tmp_accum;

// Running same kernel operation num_times to keep the kernel busy for HBM
// bandwidth testing
L_vops:
  for (int count = 0; count < num_times; count++) {
    // Auto-pipeline is going to apply pipeline to this loop
    tmp_accum = 0;
  vops1:
    for (int i = 0; i < v_size; i++) {
      tmp_read = d_hbm[i+v_access_offset];
      tmp_accum += tmp_read.data[2]; // only accum the 2 from [0:15]
    }
    tmp_read.data[0] = tmp_accum;
    d_hbm[addr_accum] = tmp_read;
  }
}
}
