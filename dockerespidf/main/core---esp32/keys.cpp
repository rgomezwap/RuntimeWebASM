#include "pxt.h"

namespace pxt
{
	
#define MAX_ARCADE_BUTTONS 16

static int  s_buttonCfgKey[MAX_ARCADE_BUTTONS];
static int  s_buttonCount = 0;

// RIC esta funcion deberia inicilizar el hardware de entrada de los botones ...
void initKeys()
{
	
	// inicializamos las tablas que tienen la info de los botones
	for (int i = 0; i < MAX_ARCADE_BUTTONS; i++)
		{
        s_buttonCfgKey[i]	= -1;
		}
    s_buttonCount = 0;	
	
}

//% expose
int pressureLevelByButtonId(int btnId, int codalId)
{
	return 0;
}

//% expose
void setupButton(int buttonId, int key)
{
	
	// La imagen que se ejecuta , nos pide configurar un boton que estara activo
	if (buttonId >= 0 && buttonId < MAX_ARCADE_BUTTONS)
		{
		// key = CFG_PIN_BTN_A / _B / _LEFT / _RIGHT / _UP / _DOWN / _MENU
        s_buttonCfgKey[buttonId] = key;
		
		TECLAS_DBG("    El codigo PXT64 configura boton id %d = key %d",buttonId,key);
		
		if (buttonId + 1 > s_buttonCount) s_buttonCount = buttonId + 1;
		}
	
}



} // namespace pxt

