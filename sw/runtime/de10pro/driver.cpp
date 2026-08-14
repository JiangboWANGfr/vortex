// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#include "driver.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

namespace {

void* g_dl_handle = nullptr;
char g_last_error[512];

void set_error(const char* message) {
  std::snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

void set_dlerror_prefix(const char* prefix) {
  const char* error = dlerror();
  if (error == nullptr) {
    set_error(prefix);
    return;
  }
  std::snprintf(g_last_error, sizeof(g_last_error), "%s: %s", prefix, error);
}

template <typename T>
bool load_symbol(T* out, const char* name) {
  dlerror();
  void* symbol = dlsym(g_dl_handle, name);
  const char* error = dlerror();
  if (error != nullptr) {
    std::snprintf(g_last_error, sizeof(g_last_error),
                  "dlsym(%s): %s", name, error);
    return false;
  }
  *out = reinterpret_cast<T>(symbol);
  return true;
}

const char* get_last_error() {
  return g_last_error;
}

} // namespace

int drv_init(de10pro_drv_api_t* drv_funcs) {
  if (drv_funcs == nullptr)
    return -1;

  std::memset(drv_funcs, 0, sizeof(*drv_funcs));
  drv_funcs->get_last_error = get_last_error;
  g_last_error[0] = '\0';

  const char* so_path = std::getenv("TERASIC_PCIE_SO_PATH");
  if (so_path == nullptr || so_path[0] == '\0')
    so_path = "terasic_pcie_qsys.so";

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
   || !load_symbol(&drv_funcs->PCIE_DmaRead, "PCIE_DmaRead")
   || !load_symbol(&drv_funcs->PCIE_DmaWrite, "PCIE_DmaWrite")) {
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
