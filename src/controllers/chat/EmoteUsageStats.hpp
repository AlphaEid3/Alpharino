// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <vector>

class QMenu;

namespace chatterino {

class Channel;
class Message;

struct EmoteUsageRow {
    QString name;
    QString provider;
    int count = 0;
};

class EmoteUsageStats
{
public:
    static EmoteUsageStats &instance();

    void observeMessage(const Channel &channel, const Message &message);

    std::vector<EmoteUsageRow> personalRows() const;
    std::vector<EmoteUsageRow> channelRows(const QString &channelName,
                                           int limit) const;

    void resetPersonal();
    void resetChannel(const QString &channelName);

private:
    EmoteUsageStats() = default;
};

void fillEmoteStatsMenu(QMenu &menu, const QString &channelName);
void addEmoteStatsMenu(QMenu &menu, const QString &channelName);

}  // namespace chatterino
