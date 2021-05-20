#include "krnl_hbm_read.h"

extern "C" {
void krnl_hbm_read(
    const v_dt *d_hbm,       // Data port
    unsigned int *d_accum,   // accumulated result
    unsigned int addr_accum, // target address of writing the accum
    // const bool flag_rw,	   // Flag to indicate R/W: 0/1
    const unsigned int size,     // Size in integer
    const unsigned int num_times // Running the same kernel operations num_times
) {
// here the gmem0 and gmem34 will not work since the port binding is specificed
// in .cfg for the certain kernel; v++2020.2 should explicitly declare bundle;
// v++2019.2 failed do that (manually multiple copy in kernel code)
#pragma HLS INTERFACE m_axi port = d_hbm offset = slave bundle = gmem0
#pragma HLS INTERFACE m_axi port = d_accum offset = slave bundle = gmem34

#pragma HLS INTERFACE s_axilite port = d_hbm
#pragma HLS INTERFACE s_axilite port = d_accum
#pragma HLS INTERFACE s_axilite port = addr_accum
// #pragma HLS INTERFACE s_axilite port = flag_rw
#pragma HLS INTERFACE s_axilite port = size
#pragma HLS INTERFACE s_axilite port = num_times
#pragma HLS INTERFACE s_axilite port = return

  unsigned int v_size = ((size - 1) / VDATA_SIZE) + 1;
  unsigned int tmp_accum;
  v_dt tmp_read;

// Running same kernel operation num_times to keep the kernel busy for HBM
// bandwidth testing
L_vops:
  for (int count = 0; count < num_times; count++) {
    // Auto-pipeline is going to apply pipeline to this loop
    tmp_accum = 0;
  vops1:
    for (int i = 0; i < v_size; i++) {
      tmp_read = d_hbm[i];
    // FIXME: will the II == 1?
    vops2:
      for (int k = 0; k < VDATA_SIZE; k++) {
        tmp_accum += tmp_read.data[k];
      }
    }
    d_accum[addr_accum] = tmp_accum;
  }
}
}
