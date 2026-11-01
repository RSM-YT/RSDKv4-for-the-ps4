#pragma once

#include "common.h"

#include <orbis/libkernel.h>

#ifdef __cplusplus
extern "C" {
#endif

OrbisKernelModule load_module_from_sandbox(const char* name, size_t args, const void* argp, unsigned int flags, void* opts, int* res);

bool get_module_base(OrbisKernelModule module, uint64_t* base, uint64_t* size);

typedef void module_patch_cb_t(void* arg, uint8_t* base, uint64_t size);
bool patch_module(OrbisKernelModule module, module_patch_cb_t* cb, void* arg);

void hexdump(const void* data, size_t size);

void send_notify(const char* format, ...);

#ifdef __cplusplus
}
#endif
