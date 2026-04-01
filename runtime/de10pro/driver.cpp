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

#include "driver.h"

#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {

void* g_dl_handle = nullptr;
char g_last_error[512];

void set_error(const char* message) {
  std::snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

void set_dlerror_prefix(const char* prefix) {
  auto* err = dlerror();
  if (err == nullptr) {
    set_error(prefix);
    return;
  }
  std::snprintf(g_last_error, sizeof(g_last_error), "%s: %s", prefix, err);
}

template <typename T>
bool load_symbol(T* out, const char* name) {
  dlerror();
  auto* sym = dlsym(g_dl_handle, name);
  auto* err = dlerror();
  if (err != nullptr) {
    std::snprintf(g_last_error, sizeof(g_last_error), "dlsym(%s): %s", name, err);
    return false;
  }
  *out = reinterpret_cast<T>(sym);
  return true;
}

const char* get_last_error() {
  return g_last_error;
}

} // namespace

int drv_init(de10pro_drv_api_t* drv_funcs) {
  if (drv_funcs == nullptr)
    return -1;

  const char* so_path = std::getenv("TERASIC_PCIE_SO_PATH");
  if (so_path == nullptr || so_path[0] == '\0') {
    so_path = "terasic_pcie_qsys.so";
  }

  dlerror();
  g_dl_handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
  if (g_dl_handle == nullptr) {
    set_dlerror_prefix("dlopen");
    return -1;
  }

  if (!load_symbol(&drv_funcs->PCIE_Open, "PCIE_Open")
   || !load_symbol(&drv_funcs->PCIE_Close, "PCIE_Close")
   || !load_symbol(&drv_funcs->PCIE_Read32, "PCIE_Read32")
   || !load_symbol(&drv_funcs->PCIE_Write32, "PCIE_Write32")
   || !load_symbol(&drv_funcs->PCIE_Read8, "PCIE_Read8")
   || !load_symbol(&drv_funcs->PCIE_Write8, "PCIE_Write8")
   || !load_symbol(&drv_funcs->PCIE_DmaRead, "PCIE_DmaRead")
   || !load_symbol(&drv_funcs->PCIE_DmaWrite, "PCIE_DmaWrite")
   || !load_symbol(&drv_funcs->PCIE_ConfigRead32, "PCIE_ConfigRead32")) {
    dlclose(g_dl_handle);
    g_dl_handle = nullptr;
    return -1;
  }

  drv_funcs->get_last_error = get_last_error;
  return 0;
}

void drv_close() {
  if (g_dl_handle != nullptr) {
    dlclose(g_dl_handle);
    g_dl_handle = nullptr;
  }
}
