/*---------------------------------------------------------*\
| BLEDOMController_Windows.cpp                              |
|                                                           |
|   Driver for ELK-BLEDOM Bluetooth LE LED strips           |
|   (Windows, WinRT Bluetooth LE GATT API)                  |
|                                                           |
|   Braian Wolkowyski                           21 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

/*---------------------------------------------------------*\
| OpenRGB builds as C++17. C++/WinRT only needs the         |
| coroutine handle types (this file never uses co_await),   |
| which MSVC still provides in C++17 mode through the       |
| deprecated <experimental/coroutine> header. This define   |
| silences that deprecation. If a future MSVC removes the   |
| header, building this file as C++20 fixes it; the define  |
| is harmless in C++20.                                     |
\*---------------------------------------------------------*/
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS

#include <chrono>
#include <cstdio>
#include <cstring>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

#include "BLEDOMController_Windows.h"
#include "LogManager.h"

#pragma comment(lib, "windowsapp")

using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Storage::Streams;

/*---------------------------------------------------------*\
| Write characteristics used by the BLEDOM firmwares        |
\*---------------------------------------------------------*/
static const winrt::guid bledom_write_uuids[] =
{
    winrt::guid(0x0000FFF3, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB }),
    winrt::guid(0x0000FFE9, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB }),
};

#define BLEDOM_WRITE_UUID_COUNT     (sizeof(bledom_write_uuids) / sizeof(bledom_write_uuids[0]))

struct BLEDOMWinRTState
{
    BluetoothLEDevice           device          { nullptr };
    GattSession                 session         { nullptr };
    GattCharacteristic          characteristic  { nullptr };
    GattWriteOption             write_option    { GattWriteOption::WriteWithoutResponse };
};

static void BuildColorFrame(unsigned char* frame, unsigned char* color)
{
    frame[0] = 0x7E;
    frame[1] = 0x07;
    frame[2] = 0x05;
    frame[3] = 0x03;
    frame[4] = color[0];
    frame[5] = color[1];
    frame[6] = color[2];
    frame[7] = 0x10;
    frame[8] = 0xEF;
}

static void BuildPowerFrame(unsigned char* frame, bool on)
{
    frame[0] = 0x7E;
    frame[1] = 0x04;
    frame[2] = 0x04;
    frame[3] = on ? 0xF0 : 0x00;
    frame[4] = 0x00;
    frame[5] = on ? 0x01 : 0x00;
    frame[6] = 0xFF;
    frame[7] = 0x00;
    frame[8] = 0xEF;
}

BLEDOMController::BLEDOMController(std::string dev_name, std::string mac)
{
    name                = dev_name;
    mac_string          = mac;
    address             = 0;
    winrt_state         = new BLEDOMWinRTState;
    worker_thread       = NULL;
    connected           = false;
    connect_failures    = 0;
    worker_running      = false;
    color_pending       = false;
    power_pending       = false;
    requested_power     = BLEDOM_POWER_UNKNOWN;

    memset(pending_color, 0, sizeof(pending_color));

    if(!ParseMAC(mac, &address))
    {
        LOG_ERROR("[BLEDOM] %s: invalid MAC address \"%s\"", name.c_str(), mac.c_str());
        return;
    }

    /*-----------------------------------------------------*\
    | Connecting can take several seconds, so it happens on |
    | the worker thread and detection returns immediately.  |
    \*-----------------------------------------------------*/
    worker_running      = true;
    worker_thread       = new std::thread(&BLEDOMController::WorkerThreadFunction, this);
}

BLEDOMController::~BLEDOMController()
{
    /*-----------------------------------------------------*\
    | A request that is still queued gets one last delivery |
    | attempt (CLI usage sets a color and exits right       |
    | away). The worker gives up after a single failed      |
    | connection or write, so shutdown never stalls on an   |
    | unreachable strip.                                    |
    \*-----------------------------------------------------*/
    if(worker_thread != NULL)
    {
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            worker_running = false;
        }
        pending_cv.notify_all();

        worker_thread->join();
        delete worker_thread;
    }

    delete winrt_state;
}

std::string BLEDOMController::GetName()
{
    return(name);
}

std::string BLEDOMController::GetMAC()
{
    return(mac_string);
}

std::string BLEDOMController::GetLocation()
{
    return("Bluetooth LE: " + mac_string);
}

void BLEDOMController::SetColor(unsigned char red, unsigned char grn, unsigned char blu)
{
    {
        std::lock_guard<std::mutex> lock(pending_mutex);

        /*-------------------------------------------------*\
        | Showing a color implies the strip must be on      |
        \*-------------------------------------------------*/
        if(requested_power != BLEDOM_POWER_ON)
        {
            requested_power = BLEDOM_POWER_ON;
            power_pending   = true;
        }

        pending_color[0]    = red;
        pending_color[1]    = grn;
        pending_color[2]    = blu;
        color_pending       = true;
    }
    pending_cv.notify_one();
}

void BLEDOMController::SetPower(bool on)
{
    int new_power = on ? BLEDOM_POWER_ON : BLEDOM_POWER_OFF;

    {
        std::lock_guard<std::mutex> lock(pending_mutex);

        if(requested_power == new_power)
        {
            return;
        }

        requested_power     = new_power;
        power_pending       = true;
    }
    pending_cv.notify_one();
}

bool BLEDOMController::ParseMAC(std::string mac, unsigned long long* out_address)
{
    unsigned int    bytes[6];
    char            trailing;

    if(sscanf(mac.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x%c",
              &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5], &trailing) != 6)
    {
        return(false);
    }

    *out_address = 0;

    for(int byte_idx = 0; byte_idx < 6; byte_idx++)
    {
        *out_address = (*out_address << 8) | (bytes[byte_idx] & 0xFF);
    }

    return(true);
}

void BLEDOMController::WorkerThreadFunction()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    unsigned char frame[BLEDOM_FRAME_SIZE];
    unsigned char color[3];

    while(true)
    {
        bool stopping;
        bool send_power;
        bool power_on;
        bool send_color;

        {
            std::lock_guard<std::mutex> lock(pending_mutex);

            if(!worker_running && !power_pending && !color_pending)
            {
                break;
            }

            stopping = !worker_running;
        }

        /*-------------------------------------------------*\
        | (Re)connect, waiting between failed attempts.     |
        | While stopping, a single attempt is made.         |
        \*-------------------------------------------------*/
        if(!connected)
        {
            if(!Connect())
            {
                if(stopping)
                {
                    break;
                }

                std::unique_lock<std::mutex> lock(pending_mutex);

                if(worker_running)
                {
                    pending_cv.wait_for(lock, std::chrono::milliseconds(BLEDOM_RECONNECT_INTERVAL));
                }
                continue;
            }
        }

        /*-------------------------------------------------*\
        | Wait for a request and take a snapshot of it      |
        \*-------------------------------------------------*/
        {
            std::unique_lock<std::mutex> lock(pending_mutex);

            while(worker_running && !power_pending && !color_pending)
            {
                pending_cv.wait(lock);
            }

            if(!power_pending && !color_pending)
            {
                continue;
            }

            send_power      = power_pending;
            power_on        = (requested_power == BLEDOM_POWER_ON);
            send_color      = color_pending && power_on;
            memcpy(color, pending_color, sizeof(color));

            power_pending   = false;
            color_pending   = false;
        }

        /*-------------------------------------------------*\
        | Power first, so a color sent after "on" is shown  |
        \*-------------------------------------------------*/
        bool success = true;

        if(send_power)
        {
            BuildPowerFrame(frame, power_on);
            success = WriteFrame(frame);

            if(success && send_color)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(BLEDOM_POWER_SETTLE_TIME));
            }
        }

        if(success && send_color)
        {
            BuildColorFrame(frame, color);
            success = WriteFrame(frame);
        }

        if(!success)
        {
            Disconnect();

            if(stopping)
            {
                break;
            }

            /*---------------------------------------------*\
            | Requeue what failed unless a newer request    |
            | already replaced it                           |
            \*---------------------------------------------*/
            std::lock_guard<std::mutex> lock(pending_mutex);

            if(send_power)
            {
                power_pending = true;
            }

            if(send_color && !color_pending)
            {
                memcpy(pending_color, color, sizeof(color));
                color_pending = true;
            }
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(BLEDOM_MIN_WRITE_INTERVAL));
    }

    /*-----------------------------------------------------*\
    | Writes without response are only queued. Closing the  |
    | link right away drops frames that were not sent yet,  |
    | so give the Bluetooth stack time to flush them.       |
    \*-----------------------------------------------------*/
    if(connected)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(BLEDOM_SHUTDOWN_DRAIN_TIME));
    }

    Disconnect();
    winrt::uninit_apartment();
}

void BLEDOMController::ReportConnectFailure(const char* reason)
{
    /*-----------------------------------------------------*\
    | Warn once, then keep retries out of the default log   |
    \*-----------------------------------------------------*/
    if(connect_failures == 0)
    {
        LOG_WARNING("[BLEDOM] %s: %s, retrying every %d s", name.c_str(), reason, BLEDOM_RECONNECT_INTERVAL / 1000);
    }
    else
    {
        LOG_VERBOSE("[BLEDOM] %s: %s", name.c_str(), reason);
    }

    connect_failures++;
}

bool BLEDOMController::Connect()
{
    char reason[128];

    try
    {
        BluetoothLEDevice device = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();

        if(!device)
        {
            ReportConnectFailure("device not found");
            return(false);
        }

        /*-------------------------------------------------*\
        | Ask Windows to keep the link up between writes    |
        \*-------------------------------------------------*/
        GattSession session = GattSession::FromDeviceIdAsync(device.BluetoothDeviceId()).get();

        if(session)
        {
            session.MaintainConnection(true);
        }

        GattDeviceServicesResult services = device.GetGattServicesAsync(BluetoothCacheMode::Uncached).get();

        if(services.Status() != GattCommunicationStatus::Success)
        {
            snprintf(reason, sizeof(reason), "GATT services unreachable (status %d), is another app connected?", (int)services.Status());
            ReportConnectFailure(reason);

            if(session)
            {
                session.Close();
            }
            device.Close();
            return(false);
        }

        IVectorView<GattDeviceService> service_list = services.Services();

        for(unsigned int service_idx = 0; service_idx < service_list.Size(); service_idx++)
        {
            GattCharacteristicsResult characteristics = service_list.GetAt(service_idx).GetCharacteristicsAsync(BluetoothCacheMode::Uncached).get();

            if(characteristics.Status() != GattCommunicationStatus::Success)
            {
                continue;
            }

            IVectorView<GattCharacteristic> characteristic_list = characteristics.Characteristics();

            for(unsigned int char_idx = 0; char_idx < characteristic_list.Size(); char_idx++)
            {
                GattCharacteristic characteristic = characteristic_list.GetAt(char_idx);

                for(unsigned int uuid_idx = 0; uuid_idx < BLEDOM_WRITE_UUID_COUNT; uuid_idx++)
                {
                    if(characteristic.Uuid() != bledom_write_uuids[uuid_idx])
                    {
                        continue;
                    }

                    GattCharacteristicProperties props = characteristic.CharacteristicProperties();

                    if((props & GattCharacteristicProperties::WriteWithoutResponse) == GattCharacteristicProperties::WriteWithoutResponse)
                    {
                        winrt_state->write_option = GattWriteOption::WriteWithoutResponse;
                    }
                    else if((props & GattCharacteristicProperties::Write) == GattCharacteristicProperties::Write)
                    {
                        winrt_state->write_option = GattWriteOption::WriteWithResponse;
                    }
                    else
                    {
                        continue;
                    }

                    winrt_state->device         = device;
                    winrt_state->session        = session;
                    winrt_state->characteristic = characteristic;
                    connected                   = true;
                    connect_failures            = 0;

                    LOG_INFO("[BLEDOM] %s: connected to %s", name.c_str(), mac_string.c_str());
                    return(true);
                }
            }
        }

        ReportConnectFailure("no BLEDOM write characteristic (FFF3/FFE9) found");

        if(session)
        {
            session.Close();
        }
        device.Close();
    }
    catch(winrt::hresult_error const& error)
    {
        snprintf(reason, sizeof(reason), "connection failed (0x%08X)", (unsigned int)error.code());
        ReportConnectFailure(reason);
    }

    return(false);
}

void BLEDOMController::Disconnect()
{
    connected = false;

    try
    {
        winrt_state->characteristic = nullptr;

        if(winrt_state->session)
        {
            winrt_state->session.Close();
        }

        if(winrt_state->device)
        {
            winrt_state->device.Close();
        }
    }
    catch(winrt::hresult_error const&)
    {
        /*-------------------------------------------------*\
        | Nothing left to clean up if closing fails         |
        \*-------------------------------------------------*/
    }

    winrt_state->session    = nullptr;
    winrt_state->device     = nullptr;
}

bool BLEDOMController::WriteFrame(unsigned char* frame)
{
    try
    {
        DataWriter writer;
        writer.WriteBytes(winrt::array_view<const unsigned char>(frame, frame + BLEDOM_FRAME_SIZE));

        GattWriteResult result = winrt_state->characteristic.WriteValueWithResultAsync(writer.DetachBuffer(), winrt_state->write_option).get();

        if(result.Status() != GattCommunicationStatus::Success)
        {
            LOG_WARNING("[BLEDOM] %s: write failed (status %d)", name.c_str(), (int)result.Status());
            return(false);
        }
    }
    catch(winrt::hresult_error const& error)
    {
        LOG_WARNING("[BLEDOM] %s: write failed (0x%08X)", name.c_str(), (unsigned int)error.code());
        return(false);
    }

    return(true);
}
