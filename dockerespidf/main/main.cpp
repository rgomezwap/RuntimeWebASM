
#include "pxt.h"

#ifdef PXT_MAIN
	
	// RIC por aqui entra
	PXT_MAIN
	
#else
	
	RIC por aqui NO entra
	
	int main() {
		uBit.init();
		pxt::start();
		release_fiber();
		return 0;   // your program will never reach this line.
	}
	
#endif
