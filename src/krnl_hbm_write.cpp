#include "krnl_hbm_write.h"

extern "C" {
void krnl_hbm_write(
    v_dt *d_hbm,       // Data port
    const unsigned int size,     // Size in integer
    const unsigned int num_times // Running the same kernel operations num_times
) {
// here the gmem0 and gmem34 will not work since the port binding is specificed
// in .cfg for the certain kernel; v++2020.2 should explicitly declare bundle;
// v++2019.2 failed do that (manually multiple copy in kernel code)
#pragma HLS INTERFACE m_axi port = d_hbm offset = slave bundle = gmem0

#pragma HLS INTERFACE s_axilite port = d_hbm
#pragma HLS INTERFACE s_axilite port = size
#pragma HLS INTERFACE s_axilite port = num_times
#pragma HLS INTERFACE s_axilite port = return

  unsigned int v_size = ((size - 1) / VDATA_SIZE) + 1;
  unsigned int tmp_accum;
  v_dt tmp_write;

// Running same kernel operation num_times to keep the kernel busy for HBM
// bandwidth testing
L_vops:
  for (int count = 0; count < num_times; count++) {
  vops1:
    for (int i = 0; i < v_size; i++) {
    // FIXME: will the II == 1?
    vops2:
      for (int k = 0; k < VDATA_SIZE; k++) {
        tmp_write.data[k] = k % VDATA_SIZE;
      }
    d_hbm[i] = tmp_write;
    }
  }
}
}
