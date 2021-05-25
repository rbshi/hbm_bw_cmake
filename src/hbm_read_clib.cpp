#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_ini.h"
#include "experimental/xrt_kernel.h"
/**
 * Trivial loopback example which runs OpenCL loopback kernel. Does not use
 * OpenCL runtime but directly exercises the XRT driver API.
 */

#define NUM_KERNEL 32

static void usage() {
  std::cout << "usage: %s [options] -k <bitstream>\n\n";
  std::cout << "  -k <bitstream>\n";
  std::cout << "  -d <device_index>\n";
  std::cout << "  -c <name of compute unit in xclbin>\n";
  std::cout << "  -v\n";
  std::cout << "  -h\n\n";
  std::cout << "";
  std::cout << "* Bitstream is required\n";
  std::cout << "* Name of compute unit from loaded xclbin is required\n";
}

static void sync_test(const xrt::device &device, int32_t grpidx) {
  std::string testVector =
      "hello\nthis is Xilinx sync BO read write test\n:-)\n";
  const size_t data_size = testVector.size();

  auto bo = xrt::bo(device, data_size, 0, grpidx);
  auto bo_data = bo.map<char *>();
  std::copy_n(testVector.begin(), data_size, bo_data);
  bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data_size, 0);
  bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, data_size, 0);
  if (!std::equal(testVector.begin(), testVector.end(), bo_data))
    throw std::runtime_error(
        "Value read back from sync bo does not match value written");
}

static void copy_test(const xrt::device &device, size_t bytes, int32_t grpidx) {
  auto bo1 = xrt::bo(device, bytes, 0, grpidx);
  auto bo1_data = bo1.map<char *>();
  std::generate_n(bo1_data, bytes, []() { return std::rand() % 256; });

  bo1.sync(XCL_BO_SYNC_BO_TO_DEVICE, bytes, 0);

  auto bo2 = xrt::bo(device, bytes, 0, grpidx);
  bo2.copy(bo1, bytes);
  bo2.sync(XCL_BO_SYNC_BO_FROM_DEVICE, bytes, 0);
  auto bo2_data = bo2.map<char *>();
  if (!std::equal(bo1_data, bo1_data + bytes, bo2_data))
    throw std::runtime_error(
        "Value read back from copy bo does not match value written");
}

static void register_test(const xrt::kernel &kernel, int argno) {
  try {
    auto offset = kernel.offset(argno);
    // Throws unless Runtime.rw_shared=true
    // Note, that xclbin must also be loaded with rw_shared=true
    auto val = kernel.read_register(offset);
    std::cout << "value at 0x" << std::hex << offset << " = 0x" << val
              << std::dec << "\n";
  } catch (const std::exception &ex) {
    std::cout << "Expected failure reading kernel register (" << ex.what()
              << ")\n";
  }
}

static void ini_test(bool failure_expected = false) {
  try {
    xrt::ini::set("Runtime.verbosity", 5);
    xrt::ini::set("Runtime.runtime_log", "console");
    xrt::ini::set("Runtime.exclusive_cu_context", true);
  } catch (const std::exception &ex) {
    if (!failure_expected)
      throw;

    std::cout << "Expected failure setting configuration options (" << ex.what()
              << ")\n";
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    usage();
    return 1;
  }

  std::string xclbin_fnm;
  std::string cu_name = "dummy";
  bool verbose = false;
  unsigned int device_index = 0;

  std::vector<std::string> args(argv + 1, argv + argc);
  std::string cur;
  for (auto &arg : args) {
    if (arg == "-h") {
      usage();
      return 1;
    } else if (arg == "-v") {
      verbose = true;
      continue;
    }

    if (arg[0] == '-') {
      cur = arg;
      continue;
    }

    if (cur == "-k")
      xclbin_fnm = arg;
    else if (cur == "-d")
      device_index = std::stoi(arg);
    else if (cur == "-c")
      cu_name = arg;
    else
      throw std::runtime_error("Unknown option value " + cur + " " + arg);
  }

  if (xclbin_fnm.empty())
    throw std::runtime_error("FAILED_TEST\nNo xclbin specified");

  // Configuration options can be change before accessed
  // ini_test();

  auto device = xrt::device(device_index);
  auto uuid = device.load_xclbin(xclbin_fnm);

  auto krnl_all = xrt::kernel(device, uuid, cu_name);

  // obtain the kernels
  // std::vector<xrt::kernel> krnls(NUM_KERNEL);
  // for (unsigned int i = 0; i < NUM_KERNEL; i++) {
  //   std::string cu_id = std::to_string(i + 1);
  //   std::string krnl_name_full = cu_name + ":{" + cu_name + "_" + cu_id +
  //   "}"; krnls[i] = xrt::kernel(device, uuid, krnl_name_full);
  // }

  // set up the hbm buffer (MAX 2GB space supported)
  unsigned long long dataSize = 1 << (6 + 10 + 10 + 3);
  // std::cout << dataSize << std::endl;
  unsigned int *host_buffer;
  // host_buffer = (unsigned int*) malloc(dataSize*sizeof(unsigned int));
  posix_memalign((void **)&host_buffer, 4096, dataSize * sizeof(unsigned int));

  // Sample example filling the allocated host memory
  for (unsigned int i = 0; i < dataSize; i++) {
    host_buffer[i] = i % 16;
  }

  std::cout << "Host buffer prepared." << std::endl;

  // open binded with krnl_all, can be access by all 32 krnls
  auto hbm_buffer =
      xrt::bo(device, host_buffer, dataSize * sizeof(unsigned int),
              krnl_all.group_id(0));
  // hbm_buffer.write(host_buffer);
  hbm_buffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
  std::cout << "Sync accomplished." << std::endl;

  std::chrono::duration<double> kernel_time(0);
  auto kernel_start = std::chrono::high_resolution_clock::now();

  unsigned int num_iter = 8;
  // run the kernel
  std::vector<xrt::run> runs(NUM_KERNEL);
  for (unsigned int i = 0; i < NUM_KERNEL; i++) {
    // offset stride = 256MB
    unsigned int access_offset = (i * (1 << 26));
    // obtain the krnl
    std::string cu_id = std::to_string(i + 1);
    std::string krnl_name_full = cu_name + ":{" + cu_name + "_" + cu_id + "}";
    // std::cout << krnl_name_full << std::endl;
    auto krnl = xrt::kernel(device, uuid, krnl_name_full, 0);
    auto run =
        krnl(hbm_buffer, access_offset, i * (1 << 22), (1 << 26), num_iter);
    runs[i] = run;
  }

  for (auto &run : runs) {
    auto state = run.wait();
  }

  auto kernel_end = std::chrono::high_resolution_clock::now();
  kernel_time = std::chrono::duration<double>(kernel_end - kernel_start);
  auto kernel_time_in_sec = kernel_time.count();
  double result = (float)2 * num_iter * sizeof(uint32_t);
  // result /= 1024;               // to KB
  // result /= 1024;               // to MB
  // result /= 1024;               // to GB
  result /= kernel_time_in_sec; // to GBps

  std::cout << NUM_KERNEL << "," << result << std::endl;

  // read out
  hbm_buffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
  std::cout << "Sync back to host accomplished." << std::endl;
  for (unsigned int i = 0; i < 8; i++) {
    std::cout << host_buffer[i * (1 << 26)] << std::endl;
  }

  // FIXME: xrt::kernel.read_register(), currently DOES NOT work with xrt 2020.2
  // auto krnl = xrt::kernel(device, uuid, "krnl_hbm_read:{krnl_hbm_read_1}");
  // for (unsigned int i = 0; i < 5; i++) {
  //   // unsigned int read_data = 0;
  //   // xrtKernelReadRegister(krnl, i, &read_data);
  //   auto offset = krnl.offset(i);
  //   unsigned int read_data = krnl.read_register(offset);
  //   std::cout << read_data << std::endl;
  // }
  return 0;
}