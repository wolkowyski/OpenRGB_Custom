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
|   Color     7E 07 05 03 RR GG BB 10 EF                    |
|   Power on  7E 04 04 F0 00 01 FF 00 EF                    |
|   Power off 7E 04 04 00 00 00 FF 00 EF                    |
|                                                           |
|   The controller accepts a single BLE connection, so the  |
|   phone app must be disconnected while OpenRGB runs.      |
\*---------------------------------------------------------*/
#define BLEDOM_FRAME_SIZE           9
#define BLEDOM_MIN_WRITE_INTERVAL   30      /* ms between writes, faster updates get dropped */
#define BLEDOM_RECONNECT_INTERVAL   5000    /* ms between reconnection attempts              */
#define BLEDOM_POWER_SETTLE_TIME    100     /* ms after a power command before the next one  */
#define BLEDOM_SHUTDOWN_DRAIN_TIME  3000    /* ms to let queued writes go out before closing */

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
    | Both calls are non-blocking. The worker thread only   |
    | sends the most recent request, so fast effects never  |
    | build up a backlog of BLE writes.                     |
    \*-----------------------------------------------------*/
    void        SetColor(unsigned char red, unsigned char grn, unsigned char blu);
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
    | pending_mutex                                         |
    \*-----------------------------------------------------*/
    std::mutex              pending_mutex;
    std::condition_variable pending_cv;
    bool                    worker_running;
    bool                    color_pending;
    unsigned char           pending_color[3];
    bool                    power_pending;
    int                     requested_power;

    void        WorkerThreadFunction();
    bool        Connect();
    void        Disconnect();
    void        ReportConnectFailure(const char* reason);
    bool        WriteFrame(unsigned char* frame);
};
