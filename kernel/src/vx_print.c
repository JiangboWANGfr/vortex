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

#include <vx_print.h>
#include <vx_spawn.h>
#include <vx_intrinsics.h>
#include "tinyprintf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	const char* format;
	va_list*    va;
	int         ret;
} printf_arg_t;

typedef struct {
	int value;
	int base;
} putint_arg_t;

typedef struct {
	float value;
	int precision;
} putfloat_arg_t;

static int local_itoa(int value, char* out, int base) {
	static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	unsigned int uvalue;
	int pos = 0;

	if (base < 2 || base > 36) {
		out[0] = '\0';
		return 0;
	}

	if (value < 0 && base == 10) {
		out[pos++] = '-';
		uvalue = (unsigned int)(-value);
	} else {
		uvalue = (unsigned int)value;
	}

	char tmp[33];
	int tpos = 0;
	do {
		tmp[tpos++] = digits[uvalue % (unsigned int)base];
		uvalue /= (unsigned int)base;
	} while (uvalue != 0);

	while (tpos > 0) {
		out[pos++] = tmp[--tpos];
	}
	out[pos] = '\0';
	return pos;
}

static void __putint_cb(const putint_arg_t* arg) {
	char tmp[33];
	local_itoa(arg->value, tmp, arg->base);
	for (int i = 0; i < 33; ++i) {
		int c = tmp[i];
		if (!c)
			break;
		vx_putchar(c);
	}
}

static void __putfloat_cb(const putfloat_arg_t* arg) {
#ifdef VX_NO_LIBC_RUNTIME
	(void)arg;
	const char* msg = "[float-disabled]";
	for (int i = 0; msg[i] != '\0'; ++i) {
		vx_putchar(msg[i]);
	}
#else
	float value = arg->value;
	int precision = arg->precision;
	int ipart = (int)value;
	vx_putint(ipart, 10);
	if (precision != 0) {
		vx_putchar('.');
		float frac = value - (float)ipart;
		float fscaled = frac * pow(10, precision);
		vx_putint((int)fscaled, 10);
	}
#endif
}

static void __vprintf_cb(printf_arg_t* arg) {
	arg->ret = tiny_vprintf(arg->format, *arg->va);
}

void vx_putint(int value, int base) {
	putint_arg_t arg;
	arg.value = value;
	arg.base = base;
	vx_serial((vx_serial_cb)__putint_cb, &arg);
}

void vx_putfloat(float value, int precision) {
	putfloat_arg_t arg;
	arg.value = value;
	arg.precision = precision;
	vx_serial((vx_serial_cb)__putfloat_cb, &arg);
}

int vx_vprintf(const char* format, va_list va) {
	printf_arg_t arg;
	arg.format = format;
	arg.va = &va;
	vx_serial((vx_serial_cb)__vprintf_cb, &arg);
  return arg.ret;
}

int vx_printf(const char * format, ...) {
	int ret;
	va_list va;
	va_start(va, format);
	ret = vx_vprintf(format, va);
	va_end(va);
  return ret;
}

#ifdef __cplusplus
}
#endif
