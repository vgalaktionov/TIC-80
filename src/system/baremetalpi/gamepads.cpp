#include "gamepads.h"
#include "utils.h"

static CUSBGamePadDevice* gamepads[4] = {NULL};

static void gamepadRemovedHandler(CDevice* device, void* context)
{
	CUSBGamePadDevice** slot = (CUSBGamePadDevice**) context;
	if (*slot == device)
	{
		*slot = NULL;
	}
}

void initGamepads(CDeviceNameService& m_DeviceNameService, TGamePadStatusHandler handler)
{
	dbg("Searching gamepads..\n");
	for (unsigned nDevice = 1; nDevice <= 4; nDevice++) // max 4 gamepads
	{
		CUSBGamePadDevice** slot = &gamepads[nDevice - 1];
		if (*slot)
		{
			continue;
		}

		CString DeviceName;
		DeviceName.Format ("upad%u", nDevice);

		*slot =
			(CUSBGamePadDevice *) m_DeviceNameService.GetDevice (DeviceName, FALSE);

		if (*slot == 0)
		{
			continue;
		}

		CUSBGamePadDevice *pGamePad = *slot;
		const TGamePadState *pState = pGamePad->GetInitialState ();

		assert (pState != 0);

		dbg("Prop %d\n", pGamePad->GetProperties());

		dbg("Gamepad %u: %d Button(s) %d Hat(s)\n", nDevice, pState->nbuttons, pState->nhats);

		for (int i = 0; i < pState->naxes; i++)
		{
			dbg("Gamepad %u: Axis %d: Minimum %d Maximum %d\n", nDevice, i+1, pState->axes[i].minimum, pState->axes[i].maximum);
		}

		pGamePad->RegisterRemovedHandler(gamepadRemovedHandler, slot);
		pGamePad->RegisterStatusHandler(handler);

	}
	dbg("Finished searching gamepads\n");

}
