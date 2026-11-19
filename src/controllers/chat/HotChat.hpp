// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace chatterino {

class HotChat
{
public:
    static HotChat &instance();

    void note(const QString &channelName);

    /// True when the recent pace is far above this channel's own baseline.
    /// Sensitivity is 1 (harder to trigger) through 10 (easier).
    bool isHot(const QString &channelName, int sensitivity) const;

private:
    HotChat() = default;
};

}  // namespace chatterino
