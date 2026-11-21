/*---------------------------------------------------------*\
| BLEDOMSettingsEntry.h                                     |
|                                                           |
|   User interface for OpenRGB ELK-BLEDOM settings entry    |
|                                                           |
|   Braian Wolkowyski                           21 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "BaseManualDeviceEntry.h"

namespace Ui
{
    class BLEDOMSettingsEntry;
}

class BLEDOMSettingsEntry : public BaseManualDeviceEntry
{
    Q_OBJECT

public:
    explicit BLEDOMSettingsEntry(QWidget *parent = nullptr);
    ~BLEDOMSettingsEntry();
    void loadFromSettings(const json& data);
    json saveSettings() override;
    bool isDataValid() override;

private:
    Ui::BLEDOMSettingsEntry *ui;

private slots:
    void changeEvent(QEvent *event) override;
};
