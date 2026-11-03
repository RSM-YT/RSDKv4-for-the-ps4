#include "util.h"

#include <stdarg.h>
#include <ctype.h>
#include <orbis/libkernel.h>

OrbisKernelModule load_module_from_sandbox(const char* name, size_t args, const void* argp, unsigned int flags, void* opts, int* res) {
	char file_path[1024];
	OrbisKernelModule module;

	snprintf(file_path, sizeof(file_path), "/%s/common/lib/%s", g_sandbox_word, name);

	module = sceKernelLoadStartModule(file_path, args, argp, flags, opts, res);

	return module;
}

bool get_module_base(OrbisKernelModule module, uint64_t* base, uint64_t* size) {
	OrbisKernelModuleInfo module_info;
	int ret;

	memset(&module_info, 0, sizeof(module_info));
	module_info.size = sizeof(module_info);
	ret = sceKernelGetModuleInfo(module, &module_info);
	if (ret) {
		EPRINTF("sceKernelGetModuleInfo(%u) failed: 0x%08X\n", module, ret);
		goto err;
	}

	if (module_info.segmentCount == 0) {
		EPRINTF("sceKernelGetModuleInfo(%u) returned no segments\n", module);
		goto err;
	}

	if (base) {
		*base = (uint64_t)module_info.segmentInfo[0].address;
	}
	if (size) {
		*size = module_info.segmentInfo[0].size;
	}
	return true;

err:
	return false;
}

bool patch_module(OrbisKernelModule module, module_patch_cb_t* cb, void* arg) {
	uint64_t base, size;
	int ret;

	if (!get_module_base(module, &base, &size)) {
		goto err;
	}

	ret = sceKernelMprotect((void*)base, size, ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_WRITE | ORBIS_KERNEL_PROT_CPU_EXEC);
	if (ret) {
		EPRINTF("sceKernelMprotect(%u) failed: 0x%08X\n", module, ret);
		goto err;
	}

	if (cb) {
		(*cb)(arg, (uint8_t*)base, size);
	}

	return true;

err:
	return false;
}

int send_notify(const char* format, ...) {
	OrbisNotificationRequest req;
	va_list args;

	memset(&req, 0, sizeof(req));
	{
		req.targetId = -1;

		va_start(args, format);
		req.message[0] = '%'; /* XXX: hackity hack */
		vsnprintf(req.message + 1, sizeof(req.message) - 1, format, args);
		va_end(args);
	}

	int result = sceKernelSendNotificationRequest(0, &req, sizeof(req), 1);
	if (result < 0) {
		EPRINTF("sceKernelSendNotificationRequest failed: 0x%08X\n", result);
	}
	return result;
}

void hexdump(const void* data, size_t size) {
	const uint8_t* p = (const uint8_t*)data;
	const size_t n = 16;
	size_t i, j, k;
	for (i = 0; i < size; i += n) {
		k = (i + n) <= size ? n : (size - i);
		printf("%8p:", (uint8_t*)data + i);
		for (j = 0; j < k; ++j) {
			printf(" %02x", p[i + j]);
		}
		for (j = k; j < n; ++j) {
			printf("   ");
		}
		printf("  ");
		for (j = 0; j < k; ++j) {
			printf("%c", isprint(p[i + j]) ? p[i + j] : '.');
		}
		for (j = k; j < n; ++j) {
			printf(" ");
		}
		printf("\n");
	}
}
