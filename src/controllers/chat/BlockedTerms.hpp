// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace chatterino {

class Channel;
class TwitchChannel;

void prepareChannelBlockedTerms(TwitchChannel *channel);

/// Empty when the message does not contain a blocked term.
QString matchingBlockedTerm(const QString &message, const Channel *channel);

}  // namespace chatterino
