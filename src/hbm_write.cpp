/**
 * Copyright (C) 2020 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

/********************************************************************************************
 * Description:
 * This is host application to test HBM Full bandwidth.
 * Design contains 8 compute units of Kernel. Each compute unit has full access
 *to all HBM
 * memory (0 to 31). Host application allocate buffers into all 32 HBM Banks(16
 *Input buffers
 * and 16 output buffers). Host application runs all 8 compute units together
 *and measure
 * the overall HBM bandwidth.
 *
 ******************************************************************************************/

#include <algorithm>
#include <iostream>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "xcl2.hpp"

// #define NUM_KERNEL 32

// HBM Pseudo-channel(PC) requirements
#define MAX_HBM_PC_COUNT 32
#define PC_NAME(n) n | XCL_MEM_TOPOLOGY
const int pc[MAX_HBM_PC_COUNT + 4] = {
    PC_NAME(0),  PC_NAME(1),  PC_NAME(2),  PC_NAME(3),  PC_NAME(4),
    PC_NAME(5),  PC_NAME(6),  PC_NAME(7),  PC_NAME(8),  PC_NAME(9),
    PC_NAME(10), PC_NAME(11), PC_NAME(12), PC_NAME(13), PC_NAME(14),
    PC_NAME(15), PC_NAME(16), PC_NAME(17), PC_NAME(18), PC_NAME(19),
    PC_NAME(20), PC_NAME(21), PC_NAME(22), PC_NAME(23), PC_NAME(24),
    PC_NAME(25), PC_NAME(26), PC_NAME(27), PC_NAME(28), PC_NAME(29),
    PC_NAME(30), PC_NAME(31), PC_NAME(34), PC_NAME(35), PC_NAME(36),
    PC_NAME(37)};

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: %s <XCLBIN> \n",
           argv[0]);
    return -1;
  }

  unsigned int NUM_KERNEL = 32;

  unsigned int dataSize =
      64 * 1024 *
      1024; // taking maximum possible data size value for an HBM bank
  unsigned int num_times =
      64; // num_times specify, number of times a kernel will execute

  // reducing the test data capacity to run faster in emulation mode
  if (xcl::is_emulation()) {
    dataSize = 1024;
    num_times = 2;
  }

  std::string binaryFile = argv[1];
  cl_int err;
  cl::CommandQueue q;
  std::string krnl_name = "krnl_hbm_write";
  std::vector<cl::Kernel> krnls(NUM_KERNEL);
  cl::Context context;
  std::vector<unsigned int, aligned_allocator<unsigned int>>
      src_d_hbm[NUM_KERNEL];
  for (int i = 0; i < NUM_KERNEL; i++) {
    src_d_hbm[i].resize(dataSize);
    std::fill(src_d_hbm[i].begin(), src_d_hbm[i].end(), 0);
  }

  // find and program the fpga (folding)
  {
    // OPENCL HOST CODE AREA START
    // The get_xil_devices will return vector of Xilinx Devices
    auto devices = xcl::get_xil_devices();

    // read_binary_file() command will find the OpenCL binary file created using
    // V++ compiler load into OpenCL Binary and return pointer to file buffer.
    auto fileBuf = xcl::read_binary_file(binaryFile);

    cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};
    bool valid_device = false;
    for (unsigned int i = 0; i < devices.size(); i++) {
      auto device = devices[i];
      // Creating Context and Command Queue for selected Device
      OCL_CHECK(err,
                context = cl::Context(device, nullptr, nullptr, nullptr, &err));
      OCL_CHECK(err,
                q = cl::CommandQueue(context, device,
                                     CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE |
                                         CL_QUEUE_PROFILING_ENABLE,
                                     &err));

      // std::cout << "Trying to program device[" << i
      //           << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
      cl::Program program(context, {device}, bins, nullptr, &err);
      if (err != CL_SUCCESS) {
        std::cout << "Failed to program device[" << i
                  << "] with xclbin file!\n";
      } else {
        // std::cout << "Device[" << i << "]: program successful!\n";
        // Creating Kernel object using Compute unit names

        for (int i = 0; i < NUM_KERNEL; i++) {
          std::string cu_id = std::to_string(i + 1);
          std::string krnl_name_full =
              krnl_name + ":{" + krnl_name + "_" + cu_id + "}";

          // printf("Creating a kernel [%s] for CU(%d)\n",
          // krnl_name_full.c_str(),
          //        i + 1);

          // Here Kernel object is created by specifying kernel name along with
          // compute unit.
          // For such case, this kernel object can only access the specific
          // Compute unit

          OCL_CHECK(err, krnls[i] =
                             cl::Kernel(program, krnl_name_full.c_str(), &err));
        }
        valid_device = true;
        break; // we break because we found a valid device
      }
    }

    if (!valid_device) {
      std::cout << "Failed to program any device found, exit!\n";
      exit(EXIT_FAILURE);
    }
  }

  std::vector<cl_mem_ext_ptr_t> d_hbm_ext(NUM_KERNEL);
  std::vector<cl::Buffer> buffer_d_hbm(NUM_KERNEL);

  // For Allocating Buffer to specific Global Memory PC, user has to use
  // cl_mem_ext_ptr_t and provide the PCs
  for (int i = 0; i < NUM_KERNEL; i++) {
    d_hbm_ext[i].obj = src_d_hbm[i].data();
    d_hbm_ext[i].param = 0;
    d_hbm_ext[i].flags = pc[i];
    OCL_CHECK(err, buffer_d_hbm[i] = cl::Buffer(
                       context,
                       CL_MEM_READ_WRITE | CL_MEM_EXT_PTR_XILINX |
                           CL_MEM_USE_HOST_PTR,
                       sizeof(uint32_t) * dataSize, &d_hbm_ext[i], &err));
  }

  // Copy input data to Device Global Memory
  for (int i = 0; i < NUM_KERNEL; i++) {
    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_d_hbm[i]},
                                                    0 /* 0 means from host*/));
  }
  q.finish();

  double kernel_time_in_sec = 0, result = 0;

  for (unsigned int num_kernel_use = 1; num_kernel_use < NUM_KERNEL + 1;
       num_kernel_use++) {
    for (unsigned int idx_access_offset_stride = 0;
         idx_access_offset_stride < 9; idx_access_offset_stride++) {
      // 8388608: 32MB / sizeof(int)
      unsigned int access_offset_stride = idx_access_offset_stride * 8388608;
      std::chrono::duration<double> kernel_time(0);
      auto kernel_start = std::chrono::high_resolution_clock::now();

      for (unsigned int i = 0; i < num_kernel_use; i++) {
        unsigned int access_offset =
            (i * access_offset_stride) % (num_kernel_use * dataSize);
        OCL_CHECK(err, err = krnls[i].setArg(0, buffer_d_hbm[0]));
        OCL_CHECK(err, err = krnls[i].setArg(1, access_offset));
        OCL_CHECK(err, err = krnls[i].setArg(2, dataSize));
        OCL_CHECK(err, err = krnls[i].setArg(3, num_times));
        // Invoking the kernel
        OCL_CHECK(err, err = q.enqueueTask(krnls[i]));
      }
      q.finish();

      auto kernel_end = std::chrono::high_resolution_clock::now();

      kernel_time = std::chrono::duration<double>(kernel_end - kernel_start);

      kernel_time_in_sec = kernel_time.count();
      kernel_time_in_sec /= num_kernel_use;

      // for (int i = 0; i < NUM_KERNEL; i++) {
      //   OCL_CHECK(err, err = q.enqueueMigrateMemObjects(
      //                      {buffer_d_hbm[i]}, CL_MIGRATE_MEM_OBJECT_HOST));
      // }
      // q.finish();

      // for (int i = 0; i < NUM_KERNEL; i++) {
      //   for (int j = 0; j < dataSize; j++) {
      //     if (src_d_hbm[i][j] != j % 16) {
      //       std::cout << "Erro @ PC[" << i << "][" << j << "]: " <<
      //       src_d_hbm[i][j] << std::endl;
      //     }
      //   }
      // }

      // Multiplying the actual data size by 4 because four buffers are being
      // used.
      result = (float)dataSize * num_times * sizeof(uint32_t);
      result /= 1000;               // to KB
      result /= 1000;               // to MB
      result /= 1000;               // to GB
      result /= kernel_time_in_sec; // to GBps

      // std::cout << "THROUGHPUT = " << result << " GB/s" << std::endl;

      std::cout << num_kernel_use << "," << access_offset_stride << ","
                << result << std::endl;
    }
  }

  // OPENCL HOST CODE AREA ENDS
  return EXIT_SUCCESS;
}
