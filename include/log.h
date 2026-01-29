/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#pragma once
#include <stdio.h>
#include <stdlib.h>

#define COLOR_ERR "\x1b[1;31m"
#define COLOR_DEBUG "\x1b[36m"
#define COLOR_RESET "\x1b[0m"

#define membrane_err(fmt, ...)                                                \
	do {                                                                  \
		fprintf(stderr,                                               \
			COLOR_ERR "[MEMBRANE] [%s:%d] " fmt COLOR_RESET "\n", \
			__FILE__, __LINE__, ##__VA_ARGS__);                   \
	} while (0)

#define membrane_assert(cond)                                        \
	do {                                                         \
		if (!(cond)) {                                       \
			membrane_err("assertion failed: %s", #cond); \
			abort();                                     \
		}                                                    \
	} while (0)

#ifndef NDEBUG
#define membrane_debug(fmt, ...)                                          \
	do {                                                              \
		fprintf(stderr,                                           \
			COLOR_DEBUG "[MEMBRANE] [%s:%d] " fmt COLOR_RESET \
				    "\n",                                 \
			__FILE__, __LINE__, ##__VA_ARGS__);               \
	} while (0)
#else
#define membrane_debug(...) ((void)0)
#endif
