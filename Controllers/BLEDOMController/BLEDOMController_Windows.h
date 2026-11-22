/*---------------------------------------------------------*\
| BLEDOMController_Windows.h                                |
|                                                           |
|   Driver for ELK-BLEDOM Bluetooth LE LED strips           |
|   (Windows, WinRT Bluetooth LE GATT API)                  |
|                                                           |
|   Braian Wolkowyski                           21 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

/*---------------------------------------------------------*\
| ELK-BLEDOM protocol                                       |
|                                                           |
|   The strip exposes service FFF0. Commands are 9 byte     |
|   frames (7E .. .. .. .. .. .. .. EF) written without     |
|   response to characteristic FFF3 (FFE9 on some           |
|   variants).                                              |
|                                                           |
|   Color       7E 07 05 03 RR GG BB 10 EF                  |
|   Power on    7E 04 04 F0 00 01 FF 00 EF                  |
|   Power off   7E 04 04 00 00 00 FF 00 EF                  |
|   Brightness  7E 04 01 BB FF FF FF 00 EF  (BB 0-100)      |
|   Speed       7E 04 02 SS FF FF FF 00 EF  (SS 0-100)      |
|   Effect      7E 05 03 EE 03 FF FF 00 EF  (EE see below)  |
|                                                           |
|   A color frame does not wake the strip from power off,   |
|   the power on frame has to be sent first. The controller |
|   accepts a single BLE connection, so the phone app must  |
|   be disconnected while OpenRGB runs.                     |
|                                                           |
|   Some firmwares ignore the brightness frame while an     |
|   effect runs (the tested strip and its phone app both    |
|   behave that way). It is still sent for the ones that    |
|   support it.                                             |
\*---------------------------------------------------------*/
#define BLEDOM_FRAME_SIZE           9
#define BLEDOM_MIN_WRITE_INTERVAL   30      /* ms between writes, faster updates get dropped */
#define BLEDOM_COMMAND_SETTLE_TIME  250     /* ms after power/effect/speed/brightness frames */
#define BLEDOM_RECONNECT_INTERVAL   5000    /* ms between reconnection attempts              */
#define BLEDOM_SHUTDOWN_DRAIN_TIME  3000    /* ms to let queued writes go out before closing */
#define BLEDOM_MAX_FRAMES           5       /* power, effect, speed, brightness, color       */

#define BLEDOM_BRIGHTNESS_MAX       100

#define BLEDOM_SPEED_MAX            100

/*---------------------------------------------------------*\
| Built-in effects                                          |
|   The single color effects take an offset into the preset |
|   color list below (red, green, blue, yellow, cyan,       |
|   purple, white).                                         |
\*---------------------------------------------------------*/
#define BLEDOM_EFFECT_JUMP_7        0x88    /* jump between 7 colors                         */
#define BLEDOM_EFFECT_BREATHING_7   0x8A    /* fade in and out, cycling through 7 colors     */
#define BLEDOM_EFFECT_BREATHING     0x8B    /* + preset index, single color breathing        */
#define BLEDOM_EFFECT_STROBE_7      0x95    /* strobe cycling through 7 colors               */
#define BLEDOM_EFFECT_STROBE        0x96    /* + preset index, single color strobe           */
#define BLEDOM_PRESET_COUNT         7

enum
{
    BLEDOM_POWER_UNKNOWN            = 0,
    BLEDOM_POWER_OFF                = 1,
    BLEDOM_POWER_ON                 = 2,
};

struct BLEDOMWinRTState;

class BLEDOMController
{
public:
    BLEDOMController(std::string dev_name, std::string mac);
    ~BLEDOMController();

    std::string GetName();
    std::string GetMAC();
    std::string GetLocation();

    /*-----------------------------------------------------*\
    | All calls are non-blocking. The worker thread only    |
    | sends the most recent request, so fast effects never  |
    | build up a backlog of BLE writes.                     |
    \*-----------------------------------------------------*/
    void        SetColor(unsigned char red, unsigned char grn, unsigned char blu);
    void        SetEffect(unsigned char effect, unsigned char speed, unsigned char brightness);
    void        SetPower(bool on);

    static bool ParseMAC(std::string mac, unsigned long long* address);

private:
    std::string             name;
    std::string             mac_string;
    unsigned long long      address;

    BLEDOMWinRTState*       winrt_state;
    std::thread*            worker_thread;

    /*-----------------------------------------------------*\
    | Only accessed by the worker thread                    |
    \*-----------------------------------------------------*/
    bool                    connected;
    unsigned int            connect_failures;

    /*-----------------------------------------------------*\
    | Shared with the worker thread, protected by           |
    | pending_mutex. requested_* hold the state the strip   |
    | should be in, *_pending mark what still has to be     |
    | sent. -1 means unknown.                               |
    \*-----------------------------------------------------*/
    std::mutex              pending_mutex;
    std::condition_variable pending_cv;
    bool                    worker_running;

    bool                    power_pending;
    int                     requested_power;
    bool                    brightness_pending;
    int                     requested_brightness;
    bool                    speed_pending;
    int                     requested_speed;
    bool                    effect_pending;
    int                     requested_effect;
    bool                    color_pending;
    unsigned char           pending_color[3];

    void        WorkerThreadFunction();
    bool        AnyPending();
    void        RequestPowerOn();
    void        RequestBrightness(int brightness);
    bool        Connect();
    void        Disconnect();
    void        ReportConnectFailure(const char* reason);
    bool        WriteFrame(unsigned char* frame);
};
