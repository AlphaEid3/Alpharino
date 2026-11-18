// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace chatterino {

class ChatPace
{
public:
    static ChatPace &instance();

    void note(const QString &channelName);

    /// Messages per second over the recent window. 0 when the channel is quiet.
    double messagesPerSecond(const QString &channelName) const;

    /// True once a fast burst's gaps say the next pause is starting.
    bool burstEnding(const QString &channelName) const;

private:
    ChatPace() = default;
};

}  // namespace chatterino
