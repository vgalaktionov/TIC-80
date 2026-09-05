//
// kernel.cpp
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "kernel.h"
#include "customscreen.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <tic80.h>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include "utils.h"
#include <limits.h>
#include <studio.h>
#include "keycodes.h"
#include "gamepads.h"
#include "syscore.h"

static const char* KN = "TIC80"; // kernel name

// currently pressed keys and modifiers
static unsigned char keyboardRawKeys[6];
static char keyboardModifiers;

// mouse status
static int mousex;
static int mousey;
static int mousewheel;
static unsigned mousebuttons;

// gamepad status
static struct TGamePadState gamepad;

static CSpinLock keyspinlock;

extern "C" {

/*
unsigned int sleep(unsigned int seconds)
{
    mTimer.SimpleMsDelay (seconds*1000);
}
*/

static struct
{

    Studio* studio;
    tic80_input input;

    struct
    {
        bool state[tic_keys_count];
    } keyboard;

    char* clipboard;
    u32* screenBuffer;

} platform;

void tic_sys_clipboard_set(const char* text)
{
    if(platform.clipboard)
    {
        free(platform.clipboard);
        platform.clipboard = NULL;
    }

    platform.clipboard = strdup(text);
}

bool tic_sys_clipboard_has()
{
    return platform.clipboard != NULL;
}

char* tic_sys_clipboard_get()
{
    return platform.clipboard ? strdup(platform.clipboard) : NULL;
}

void tic_sys_clipboard_free(const char* text)
{
    free((void*)text);
}

u64 tic_sys_counter_get()
{
    return CTimer::Get()->GetTicks();
}

u64 tic_sys_freq_get()
{
    return HZ;
}

void tic_sys_fullscreen_set(bool value)
{
}

bool tic_sys_fullscreen_get()
{
}

void tic_sys_message(const char* title, const char* message)
{
}

void tic_sys_title(const char* title)
{
}

void tic_sys_open_path(const char* path) {}
void tic_sys_open_url(const char* url) {}

void tic_sys_preseed()
{
#if defined(__TIC_MACOSX__)
    srandom(time(NULL));
    random();
#else
    srand(time(NULL));
    rand();
#endif
}

void tic_sys_update_config()
{

}

void tic_sys_default_mapping(tic_mapping* mapping)
{
    *mapping = (tic_mapping)
    {
        tic_key_up,
        tic_key_down,
        tic_key_left,
        tic_key_right,

        tic_key_z, // a
        tic_key_x, // b
        tic_key_a, // x
        tic_key_s, // y
    };
}

bool tic_sys_keyboard_text(char* text)
{
    return false;
}

void screenCopy(CScreenDevice* screen, const u32* source, bool crt)
{
    const unsigned width = TIC80_WIDTH * TIC80_BAREMETAL_SCREEN_SCALE;
    const unsigned height = TIC80_HEIGHT * TIC80_BAREMETAL_SCREEN_SCALE;

    tic80_baremetal_render(platform.screenBuffer, width, source, crt);

    if (screen->GetPitch() == width)
    {
        memcpy(screen->GetBuffer(), platform.screenBuffer, width * height * sizeof(u32));
    }
    else
    {
        for (unsigned row = 0; row < height; row++)
        {
            memcpy(screen->GetBuffer() + screen->GetPitch() * row,
                   platform.screenBuffer + width * row, width * sizeof(u32));
        }
    }
}


} //extern C

void mouseStatusHandler (unsigned nButtons, int nDisplacementX, int nDisplacementY, int nWheelMove)
{
    keyspinlock.Acquire();

    mousex += nDisplacementX;
    mousey += nDisplacementY;

    if (mousex < 0) mousex = 0;
    if (mousex > TIC80_WIDTH*MOUSE_SENS) mousex = TIC80_WIDTH*MOUSE_SENS;
    if (mousey < 0) mousey = 0;
    if (mousey > TIC80_HEIGHT*MOUSE_SENS) mousey = TIC80_HEIGHT*MOUSE_SENS;

    mousewheel += nWheelMove;
    if (mousewheel < -32) mousewheel = -32;
    if (mousewheel > 31) mousewheel = 31;

    mousebuttons = nButtons;
    keyspinlock.Release();
}

void gamePadStatusHandler (unsigned nDeviceIndex, const TGamePadState *pState)
{

    keyspinlock.Acquire();
    // Just copy buttons and axes.
    gamepad.buttons = pState -> buttons;
    gamepad.naxes = pState -> naxes;
    for (int i = 0; i< pState->naxes; i++)
    {
        gamepad.axes[i].value = pState -> axes[i].value;
    }
    keyspinlock.Release();
}


void inputToTic()
{
    tic80_input* tic_input = &platform.input;

    // mouse
    if (mousebuttons & 0x01) tic_input->mouse.left = true; else tic_input->mouse.left = false;
    if (mousebuttons & 0x02) tic_input->mouse.right = true; else tic_input->mouse.right = false;
    if (mousebuttons & 0x04) tic_input->mouse.middle = true; else tic_input->mouse.middle = false;
    tic_input->mouse.x = mousex/MOUSE_SENS + TIC80_OFFSET_LEFT;
    tic_input->mouse.y = mousey/MOUSE_SENS + TIC80_OFFSET_TOP;
    tic_input->mouse.scrollx = 0;
    tic_input->mouse.scrolly = mousewheel;
    mousewheel = 0;

    // keyboard
    tic_input->gamepads.first.data = 0;
    tic_input->keyboard.data = 0;

    u32 keynum = 0;

    //dbg("MODIF %02x ", keyboardModifiers);

    if(keyboardModifiers & 0x11) tic_input->keyboard.keys[keynum++]= tic_key_ctrl;
    if(keyboardModifiers & 0x22) tic_input->keyboard.keys[keynum++]= tic_key_shift;
    if(keyboardModifiers & 0x44) tic_input->keyboard.keys[keynum++]= tic_key_alt;

    for (unsigned i = 0; i < 6; i++)
    {
        if (keyboardRawKeys[i] != 0)
        {
            // keyboard
            if(keynum<TIC80_KEY_BUFFER){
                tic_keycode tkc = TicKeyboardCodes[keyboardRawKeys[i]];
                if(tkc != tic_key_unknown) tic_input->keyboard.keys[keynum++]= tkc;
            }
            // key to gamepad
            switch(keyboardRawKeys[i])
            {
                case 0x1d: tic_input->gamepads.first.a = true; break;
                case 0x1b: tic_input->gamepads.first.b = true; break;
                case 0x52: tic_input->gamepads.first.up = true; break;
                case 0x51: tic_input->gamepads.first.down = true; break;
                case 0x50: tic_input->gamepads.first.left = true; break;
                case 0x4F: tic_input->gamepads.first.right = true; break;
            }
            //dbg(" %02x ", keyboardRawKeys[i]);
        }
    }
    //dbg("\n");

    if(keynum >= TIC80_KEY_BUFFER) return;

    // gamepads

    if (gamepad.buttons & 0x100) tic_input->gamepads.first.a = true;
    if (gamepad.buttons & 0x200) tic_input->gamepads.first.b = true;
    if (gamepad.buttons & 0x400) tic_input->gamepads.first.x = true;
    if (gamepad.buttons & 0x800) tic_input->gamepads.first.y = true;
    // map ESC to a gamepad button to exit the game
    if (gamepad.buttons & 0x10) tic_input->keyboard.keys[keynum++]= tic_key_escape;

    // TODO use min and max instead of hardcoded range
    if (gamepad.naxes > 0)
    {
        if (gamepad.axes[0].value < 50) tic_input->gamepads.first.left = true;
        if (gamepad.axes[0].value > 200) tic_input->gamepads.first.right = true;

    }
    if (gamepad.naxes > 1)
    {
        if (gamepad.axes[1].value < 50) tic_input->gamepads.first.up = true;
        if (gamepad.axes[1].value > 200) tic_input->gamepads.first.down = true;
    }
/*
        dbg("G:BTN 0x%X", gamepad.buttons);

        if (gamepad.naxes > 0)
        {
                dbg(" A");

                for (int i = 0; i < gamepad.naxes; i++)
                {
                        dbg(" %d", gamepad.axes[i].value);
                }
        }

        if (gamepad.nhats > 0)
        {
                dbg(" H");

                for (int i = 0; i < gamepad.nhats; i++)
                {
                        dbg(" %d", gamepad.hats[i]);
                }
        }

       dbg("\n");
*/

}
void KeyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
#ifdef SERIAL_DEBUG
    static boolean firstKeyReport = true;
    boolean hasKey = ucModifiers != 0;
    for (unsigned i = 0; i < 6 && !hasKey; i++)
    {
        hasKey = RawKeys[i] != 0;
    }
    if (firstKeyReport && hasKey)
    {
        serialDebug("[tic80] keyboard input: ok\n");
        firstKeyReport = false;
    }
#endif

    // this gets called with whatever key is currently pressed in RawKeys
    // (plus modifiers).
    keyspinlock.Acquire();
    keyboardModifiers = ucModifiers;
    memcpy(keyboardRawKeys, RawKeys, sizeof(unsigned char)*6);
    keyspinlock.Release();
}

static void keyboardRemovedHandler(CDevice*, void*)
{
    keyspinlock.Acquire();
    pKeyboard = NULL;
    keyboardModifiers = 0;
    memset(keyboardRawKeys, 0, sizeof keyboardRawKeys);
    keyspinlock.Release();
    serialDebug("[tic80] keyboard: removed\n");
}

static void mouseRemovedHandler(CDevice*, void*)
{
    keyspinlock.Acquire();
    pMouse = NULL;
    mousebuttons = 0;
    mousewheel = 0;
    keyspinlock.Release();
    serialDebug("[tic80] mouse: removed\n");
}

static void updateUSBInputDevices()
{
    static boolean firstUpdate = true;
    static unsigned rescanFrames = 0;
    static boolean firstFallbackScan = true;

    if (firstUpdate)
    {
        serialDebug("[tic80] USB plug-and-play scan: begin\n");
    }

    boolean updated = mDWHCI.UpdatePlugAndPlay();
    boolean rescanned = false;

    if (firstUpdate)
    {
        serialDebug("[tic80] USB plug-and-play scan: returned\n");
        firstUpdate = false;
    }

    // Some KVM hubs do not signal downstream port changes reliably. Poll the
    // hub tree at a low rate so devices that appear after hub setup are found.
    if ((!pKeyboard || !pMouse) && ++rescanFrames >= TIC80_FRAMERATE)
    {
        rescanFrames = 0;

        if (firstFallbackScan)
        {
            serialDebug("[tic80] USB fallback rescan: begin\n");
        }

        mDWHCI.ReScanDevices();
        rescanned = true;

        if (firstFallbackScan)
        {
            serialDebug("[tic80] USB fallback rescan: returned\n");
            firstFallbackScan = false;
        }
    }
    else if (pKeyboard && pMouse)
    {
        rescanFrames = 0;
    }

    if (!updated && !rescanned)
    {
        return;
    }

    if (updated)
    {
        serialDebug("[tic80] USB topology: changed\n");
    }

    if (!pKeyboard)
    {
        pKeyboard = (CUSBKeyboardDevice *) mDeviceNameService.GetDevice("ukbd1", FALSE);
        if (pKeyboard)
        {
            pKeyboard->RegisterRemovedHandler(keyboardRemovedHandler);
            pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw);
            serialDebug("[tic80] keyboard: attached\n");
        }
    }

    if (!pMouse)
    {
        pMouse = (CMouseDevice *) mDeviceNameService.GetDevice("mouse1", FALSE);
        if (pMouse)
        {
            pMouse->RegisterRemovedHandler(mouseRemovedHandler);
            pMouse->RegisterStatusHandler(mouseStatusHandler);
            serialDebug("[tic80] mouse: attached\n");
        }
    }

    initGamepads(mDeviceNameService, gamePadStatusHandler);
}

static void updateUSBSoundDevice()
{
    CDevice* audioControl = mDeviceNameService.GetDevice("uaudioctl1", FALSE);

    if (!audioControl)
    {
        if (mUSBSound && !mUSBSound->IsActive())
        {
            delete mUSBSound;
            mUSBSound = NULL;
            serialDebug("[tic80] USB sound: removed\n");
        }

        return;
    }

    if (!mUSBSound)
    {
        serialDebug("[tic80] USB sound: begin\n");
        mUSBSound = new CUSBSoundBaseDevice(SAMPLE_RATE);
        if (!mUSBSound->AllocateQueue(1000))
        {
            delete mUSBSound;
            mUSBSound = NULL;
            serialDebug("[tic80] USB sound: queue allocation FAILED\n");
            return;
        }

        // TIC-80 produces signed 16-bit stereo. Circle maps it to the first
        // two USB output channels and silences any additional channels.
        mUSBSound->SetWriteFormat(SoundFormatSigned16, 2);
    }

    if (!mUSBSound->IsActive())
    {
        if (mUSBSound->Start())
        {
            serialDebug("[tic80] USB sound: attached\n");
        }
        else
        {
            serialDebug("[tic80] USB sound: start deferred\n");
        }
    }
}

CNetSubSystem* getTIC80NetSubsystem()
{
    return mNetworkInitialized ? &mNet : NULL;
}

void tic80SerialDebug(const char* message)
{
    serialDebug(message);
}

static void updateNetworkStatus()
{
    static boolean reported = false;

    if (!reported && mNetworkInitialized && mNet.IsRunning())
    {
        CString address;
        mNet.GetConfig()->GetIPAddress()->Format(&address);

        CString message;
        message.Format("[tic80] Wi-Fi: connected, IP %s\n", (const char*)address);
        serialDebug((const char*)message);
        reported = true;
    }
}

TShutdownMode Run(void)
{
    if (!initializeCore())
    {
        serialDebug("[tic80] core initialization: FAILED; halting\n");
        return ShutdownHalt;
    }
    mLogger.Write (KN, LogNotice, "TIC80 Port");

    serialDebug("[tic80] studio_create: begin\n");

    dbg("Calling studio init instance..\n");

    char arg0[] = "xxkernel";
    char* argv[] = {&arg0[0], NULL};
    platform.studio = studio_create(1, argv, 44100, TIC80_PIXEL_COLOR_BGRA8888,
                                    TIC80_STORAGE_ROOT, INT32_MAX, tic_layout_qwerty);
    dbg("studio_create OK\n");
    serialDebug("[tic80] studio_create: returned\n");

    if( !platform.studio)
    {
        Die("Could not init studio");
        return ShutdownHalt;
    }
    serialDebug("[tic80] studio_create: ok\n");

    dbg("Studio init ok..\n");

    const tic80* product = &studio_mem(platform.studio)->product;
    tic80_input* tic_input = &platform.input;
    tic_input->keyboard.data = 0;

    platform.screenBuffer = (u32*) malloc(TIC80_WIDTH * TIC80_HEIGHT
                                          * TIC80_BAREMETAL_SCREEN_SCALE
                                          * TIC80_BAREMETAL_SCREEN_SCALE * sizeof(u32));
    if (!platform.screenBuffer)
    {
        Die("Could not allocate video buffer");
        return ShutdownHalt;
    }

    // sound system
    mSound->AllocateQueue(1000);
    mSound->SetWriteFormat(SoundFormatSigned16);

    serialDebug("[tic80] sound: begin\n");
    if (!mSound->Start())
    {
        Die("Could not start sound.");
        return ShutdownHalt;
    }
    serialDebug("[tic80] sound: ok\n");

    // MAIN LOOP
#ifdef SERIAL_DEBUG
    boolean firstFrame = true;
#endif
    serialDebug("[tic80] main loop: entered\n");
    while(true)
    {
        keyspinlock.Acquire();
        inputToTic();
        keyspinlock.Release();

        studio_tick(platform.studio, platform.input);
#ifdef SERIAL_DEBUG
        if (firstFrame) serialDebug("[tic80] first tick: ok\n");
#endif
        studio_sound(platform.studio);
#ifdef SERIAL_DEBUG
        if (firstFrame) serialDebug("[tic80] first sound mix: ok\n");
#endif

        mSound->Write(product->samples.buffer, product->samples.count * TIC80_SAMPLESIZE);
        if (mUSBSound && mUSBSound->IsActive())
        {
            mUSBSound->Write(product->samples.buffer,
                             product->samples.count * TIC80_SAMPLESIZE);
        }
#ifdef SERIAL_DEBUG
        if (firstFrame) serialDebug("[tic80] first audio write: ok\n");
#endif

        mScreen.vsync();
#ifdef SERIAL_DEBUG
        if (firstFrame) serialDebug("[tic80] first vsync: ok\n");
#endif

        screenCopy(&mScreen, product->screen, studio_config(platform.studio)->options.crt);
#ifdef SERIAL_DEBUG
        if (firstFrame)
        {
            serialDebug("[tic80] first frame copy: ok\n");
            firstFrame = false;
        }
#endif

        updateUSBInputDevices();
        updateUSBSoundDevice();
        updateNetworkStatus();

        mScheduler.Yield(); // for sound
    }

    return ShutdownHalt;
}
