/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#pragma once
#include <stdio.h>
#include <stdlib.h>

#define membrane_err(fmt, ...)                                           \
	do {                                                             \
		fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, \
			##__VA_ARGS__);                                  \
	} while (0)

#define membrane_assert(cond)                                        \
	do {                                                         \
		if (!(cond)) {                                       \
			membrane_err("assertion failed: %s", #cond); \
			abort();                                     \
		}                                                    \
	} while (0)

#ifndef NDEBUG
#define membrane_debug(...) membrane_err(__VA_ARGS__)
#else
#define membrane_debug(...) ((void)0)
#endif
