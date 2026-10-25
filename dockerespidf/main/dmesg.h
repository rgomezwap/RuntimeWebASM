#pragma once

#include <stddef.h>

#ifndef DMESG

	#ifdef __cplusplus
	
		RIC por aqui NO entra 1
	
		extern "C" void dmesg(const char *fmt, ...);
		#define DMESG ::dmesg

	#else	// not C++
	
		RIC por aqui NO entra 2

		void dmesg(const char *fmt, ...);
		#define DMESG dmesg

	#endif
	
#endif
