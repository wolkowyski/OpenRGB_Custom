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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

/*---------------------------------------------------------*\
| ELK-BLEDOM protocol                                       |
|   Every command is a 9 byte frame 7E .. .. .. .. .. .. EF |
|   written to characteristic FFF3 (or FFE9 on some         |
|   variants) without response.                             |
\*---------------------------------------------------------*/
#define BLEDOM_FRAME_SIZE           9
#define BLEDOM_MIN_WRITE_INTERVAL   30      /* ms between writes, the strip drops faster updates */
#define BLEDOM_RECONNECT_INTERVAL   5000    /* ms between reconnection attempts                  */
#define BLEDOM_SHUTDOWN_FLUSH_TIME  8000    /* ms allowed on shutdown to deliver a pending color */

struct BLEDOMWinRTState;

class BLEDOMController
{
public:
    BLEDOMController(const std::string& dev_name, const std::string& mac);
    ~BLEDOMController();

    std::string GetName();
    std::string GetMAC();
    std::string GetLocation();
    bool        IsConnected();

    /*-----------------------------------------------------*\
    | Queue a color. Non-blocking: the worker thread only   |
    | sends the most recent color, so fast effects never    |
    | build up a backlog of BLE writes.                     |
    \*-----------------------------------------------------*/
    void        SetColor(unsigned char red, unsigned char grn, unsigned char blu);

    static bool ParseMAC(const std::string& mac, uint64_t& address);

private:
    std::string                         name;
    std::string                         mac_string;
    uint64_t                            address;

    std::unique_ptr<BLEDOMWinRTState>   winrt_state;

    std::thread                         worker_thread;
    std::atomic<bool>                   worker_running;
    std::atomic<bool>                   connected;
    std::chrono::steady_clock::time_point flush_deadline;

    std::mutex                          pending_mutex;
    std::condition_variable             pending_cv;
    bool                                pending;
    unsigned char                       pending_frame[BLEDOM_FRAME_SIZE];

    void        WorkerThreadFunction();
    bool        Connect();
    void        Disconnect();
    bool        WriteFrame(const unsigned char* frame);
};
