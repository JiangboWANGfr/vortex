// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>
#include <fstream>
#include <iomanip>
#include <unistd.h>
#include <algorithm>
#include <util.h>
#include <mem.h>
#include <VX_config.h>
#include <VX_types.h>
#include "processor.h"

#define RAM_PAGE_SIZE 4096

using namespace vortex;

static void show_usage() {
   std::cout << "Usage: [-h: help] <program>" << std::endl;
}

const char* program = nullptr;

static void dump_perf(vortex::RAM& ram) {
  uint64_t total_instrs = 0;
  uint64_t max_cycles = 0;

  for (uint32_t core_id = 0; core_id < NUM_CORES; ++core_id) {
    uint64_t cycles = 0;
    uint64_t instrs = 0;
    uint64_t mpm_mem_addr = IO_MPM_ADDR + core_id * 32 * sizeof(uint64_t);

    ram.read(&cycles, mpm_mem_addr + (VX_CSR_MCYCLE - VX_CSR_MPM_BASE) * sizeof(uint64_t), sizeof(uint64_t));
    ram.read(&instrs, mpm_mem_addr + (VX_CSR_MINSTRET - VX_CSR_MPM_BASE) * sizeof(uint64_t), sizeof(uint64_t));

    float ipc = (cycles != 0) ? (float(instrs) / float(cycles)) : 0.0f;
    if (NUM_CORES > 1) {
      std::cout << "PERF: core" << core_id
                << ": instrs=" << instrs
                << ", cycles=" << cycles
                << ", IPC=" << ipc << std::endl;
    }

    total_instrs += instrs;
    max_cycles = std::max(max_cycles, cycles);
  }

  float ipc = (max_cycles != 0) ? (float(total_instrs) / float(max_cycles)) : 0.0f;
  std::cout << "PERF: instrs=" << total_instrs
            << ", cycles=" << max_cycles
            << ", IPC=" << ipc << std::endl;
}

static void parse_args(int argc, char **argv) {
  	int c;
  	while ((c = getopt(argc, argv, "rh")) != -1) {
    	switch (c) {
    	case 'h':
      	show_usage();
      	exit(0);
    	default:
      		show_usage();
      		exit(-1);
    	}
	}

	if (optind < argc) {
		program = argv[optind];
		std::cout << "Running " << program << "..." << std::endl;
	} else {
		show_usage();
      	exit(-1);
	}
}

int main(int argc, char **argv) {
	int exitcode = 0;

	parse_args(argc, argv);

	// create memory module
	vortex::RAM ram(0, RAM_PAGE_SIZE);

	// create processor
	vortex::Processor processor;

	// attach memory module
	processor.attach_ram(&ram);

	// setup base DCRs
	const uint64_t startup_addr(STARTUP_ADDR);
	processor.dcr_write(VX_DCR_BASE_STARTUP_ADDR0, startup_addr & 0xffffffff);
#if (XLEN == 64)
    processor.dcr_write(VX_DCR_BASE_STARTUP_ADDR1, startup_addr >> 32);
#endif
	processor.dcr_write(VX_DCR_BASE_MPM_CLASS, 0);

	// load program
	{
		std::string program_ext(fileExtension(program));
		if (program_ext == "bin") {
			ram.loadBinImage(program, startup_addr);
		} else if (program_ext == "hex") {
			ram.loadHexImage(program);
		} else {
			std::cerr << "Error: only *.bin or *.hex images supported." << std::endl;
			return -1;
		}
	}
#ifndef NDEBUG
	std::cout << "[VXDRV] START: program=" << program << std::endl;
#endif
	// run simulation
	processor.run();

  dump_perf(ram);

	// read exitcode from @MPM.1
  ram.read(&exitcode, (IO_MPM_ADDR + 8), 4);

	return exitcode;
}
