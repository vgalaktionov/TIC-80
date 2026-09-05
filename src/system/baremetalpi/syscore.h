#pragma once

#define HDMI_SOUND 1

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/nulldevice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include "customscreen.h"
#include <tic80.h>
#include "utils.h"
#include <circle/serial.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/sound/usbsoundbasedevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>

#ifdef HDMI_SOUND
  #include <vc4/vchiq/vchiqdevice.h>
  #include <vc4/sound/vchiqsoundbasedevice.h>
#else
  #include <circle/sound/pwmsoundbasedevice.h>
#endif

#include <circle/input/mouse.h>
#include <circle/input/console.h>
#include <circle/sched/scheduler.h>
#include <circle/net/netsubsystem.h>
#include <circle/startup.h>

#include <circle_glue.h>


// mouse sensitivity
#define MOUSE_SENS 2
#define SAMPLE_RATE 44100
#define CHUNK_SIZE 4000
#define WLAN_FIRMWARE_PATH "SD:/firmware/"
#define WLAN_CONFIG_FILE "SD:/wpa_supplicant.conf"

static        CActLED            mActLED;
static        CKernelOptions     mOptions;
static       CDeviceNameService mDeviceNameService;
static        CNullDevice        mNullDevice;
static        CExceptionHandler  mExceptionHandler;
static        CInterruptSystem   mInterrupt;
#ifdef EN_DEBUG
// show a larger screen, so the actual screen is on the top left
// and output is readable
static	CScreenDevice      mScreen(1280,720);
#else
static	CScreenDevice      mScreen(TIC80_WIDTH, TIC80_HEIGHT);
#endif
#ifdef SERIAL_DEBUG
static        CSerialDevice      mSerial;
#else
static        CSerialDevice      mSerial(&mInterrupt);
#endif
static        CTimer             mTimer(&mInterrupt);
#ifdef SERIAL_DEBUG
static        CLogger		mLogger(LogDebug, &mTimer);
#else
static        CLogger		mLogger(LogWarning /*mOptions.GetLogLevel ()*/, &mTimer);
#endif
static        CUSBHCIDevice	mDWHCI (&mInterrupt, &mTimer, TRUE);
static        CEMMCDevice     mEMMC(&mInterrupt, &mTimer, &mActLED);
static        CConsole        mConsole(&mScreen);
static	FATFS		mSDFileSystem;
static	FATFS		mUSBFileSystem;
static	CScheduler		mScheduler;
static CBcm4343Device mWLAN(WLAN_FIRMWARE_PATH);
static CNetSubSystem mNet(NULL, NULL, NULL, NULL, "tic80", NetDeviceTypeWLAN);
static CWPASupplicant mWPASupplicant(WLAN_CONFIG_FILE);
static CUSBKeyboardDevice *pKeyboard = NULL;
static CMouseDevice *pMouse= NULL;

static CSoundBaseDevice	*mSound;
static CUSBSoundBaseDevice *mUSBSound = NULL;
static const char* TIC80_STORAGE_ROOT = "SD:/tic80";
static boolean mNetworkInitialized = false;

#ifdef SERIAL_DEBUG
static boolean mSerialReady = false;

static void serialDebug(const char* message)
{
	if (mSerialReady)
	{
		mSerial.Write(message, strlen(message));
	}
}
#else
static void serialDebug(const char*)
{
}
#endif

boolean Die(const char *msg)
{
	serialDebug("[tic80] FATAL: ");
	serialDebug(msg);
	serialDebug("\n");
	dbg("FATAL\n");
	dbg(msg);
	dbg("\n");
#ifndef SERIAL_DEBUG
	CTimer::SimpleMsDelay(100000);
#endif
	return false;
}


boolean initializeCore()
{
#ifdef SERIAL_DEBUG
	if (!mSerial.Initialize(115200))
	{
		return false;
	}
	mSerialReady = true;
	serialDebug("[tic80] serial: ok (115200 8N1, polling)\n");
#endif

	if (!mInterrupt.Initialize())
	{
		serialDebug("[tic80] interrupt: FAILED\n");
		return false;
	}
	serialDebug("[tic80] interrupt: ok\n");

#ifndef SERIAL_DEBUG
	if (!mSerial.Initialize(921600))
	{
		return false;
	}
	mSerial.RegisterMagicReceivedHandler("REBOOT", reboot); // for hot deploy
#endif

	serialDebug("[tic80] screen: begin\n");
	if (!mScreen.Initialize())
	{
		serialDebug("[tic80] screen: FAILED\n");
		return false;
	}
	serialDebug("[tic80] screen: ok\n");

#ifdef SERIAL_DEBUG
	if (!mLogger.Initialize(&mSerial))
#else
	if (!mLogger.Initialize(&mNullDevice))
#endif
	{
		serialDebug("[tic80] logger: FAILED\n");
		return false;
	}
	serialDebug("[tic80] logger: ok\n");

	serialDebug("[tic80] timer: begin\n");
	if (!mTimer.Initialize())
	{
		serialDebug("[tic80] timer: FAILED\n");
		return false;
	}
	serialDebug("[tic80] timer: ok\n");

	serialDebug("[tic80] emmc: begin\n");
	if (!mEMMC.Initialize())
	{
		serialDebug("[tic80] emmc: FAILED\n");
		return false;
	}
	serialDebug("[tic80] emmc: ok\n");

	serialDebug("[tic80] usb: begin\n");
	if (!mDWHCI.Initialize(FALSE))
	{
		serialDebug("[tic80] usb: FAILED\n");
		return false;
	}
	serialDebug("[tic80] usb: ok (device scan deferred)\n");

	serialDebug("[tic80] console: begin\n");
	if (!mConsole.Initialize())
	{
		serialDebug("[tic80] console: FAILED\n");
		return false;
	}
	serialDebug("[tic80] console: ok\n");

#ifdef HDMI_SOUND
	serialDebug("[tic80] vchiq memory: begin\n");
	CMemorySystem *mMemory = new CMemorySystem();
	serialDebug("[tic80] vchiq memory: ok\n");
	serialDebug("[tic80] vchiq device: begin\n");
	CVCHIQDevice *mVCHIQ = new CVCHIQDevice(mMemory, &mInterrupt);
	serialDebug("[tic80] vchiq device: ok\n");
	serialDebug("[tic80] vchiq initialize: begin\n");
	if (!mVCHIQ->Initialize())
	{
		serialDebug("[tic80] vchiq initialize: FAILED\n");
		return false;
	}
	serialDebug("[tic80] vchiq initialize: ok\n");
	serialDebug("[tic80] HDMI sound device: begin\n");
	mSound = new CVCHIQSoundBaseDevice(mVCHIQ, SAMPLE_RATE, CHUNK_SIZE,
		(TVCHIQSoundDestination) mOptions.GetSoundOption());
	serialDebug("[tic80] HDMI sound device: ok\n");
#else
	mSound = new CPWMSoundBaseDevice(&mInterrupt, SAMPLE_RATE, CHUNK_SIZE);
#endif

	CGlueStdioInit(mConsole);
	serialDebug("[tic80] stdio: ok\n");

	serialDebug("[tic80] mount SD: begin\n");
	if (f_mount(&mSDFileSystem, "SD:", 1) != FR_OK)
	{
		serialDebug("[tic80] mount SD: FAILED\n");
		return Die("Cannot mount SD drive");
	}
	serialDebug("[tic80] mount SD: ok\n");

	serialDebug("[tic80] mount USB: begin\n");
	if (f_mount(&mUSBFileSystem, "USB:", 1) != FR_OK)
	{
		serialDebug("[tic80] mount USB: unavailable; keeping SD\n");
	}
	else
	{
		serialDebug("[tic80] mount USB: ok\n");
	}

	serialDebug("[tic80] storage folder: begin\n");
	FRESULT folderResult = f_mkdir(TIC80_STORAGE_ROOT);
	if (folderResult != FR_OK && folderResult != FR_EXIST)
	{
		serialDebug("[tic80] storage folder: FAILED\n");
		return Die("Cannot create TIC-80 storage folder");
	}
	serialDebug("[tic80] storage folder: ok\n");

	FILINFO wifiConfig;
	if (f_stat(WLAN_CONFIG_FILE, &wifiConfig) == FR_OK)
	{
		serialDebug("[tic80] Wi-Fi: firmware begin\n");
		if (!mWLAN.Initialize())
		{
			serialDebug("[tic80] Wi-Fi: firmware FAILED\n");
		}
		else if (!mNet.Initialize(FALSE))
		{
			serialDebug("[tic80] Wi-Fi: network stack FAILED\n");
		}
		else if (!mWPASupplicant.Initialize())
		{
			serialDebug("[tic80] Wi-Fi: supplicant FAILED\n");
		}
		else
		{
			mNetworkInitialized = true;
			serialDebug("[tic80] Wi-Fi: connecting\n");
		}
	}
	else
	{
		serialDebug("[tic80] Wi-Fi: no wpa_supplicant.conf; disabled\n");
	}

	CScreenDevice* screen = &mScreen;
	dbg("Screen is:\n");
	dbg("%d x %d pitch: %d\n", screen->GetWidth(), screen->GetHeight(), screen->GetPitch());

	serialDebug("[tic80] core initialization: ok\n");
	return true;
}
