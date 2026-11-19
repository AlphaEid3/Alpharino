// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/buttons/Button.hpp"

#include <QHBoxLayout>
#include <QLabel>

namespace chatterino {

class LabelButton : public Button
{
public:
    LabelButton(const QString &text = {}, BaseWidget *parent = nullptr,
                QSize padding = {6, 0});

    [[nodiscard]] QString text() const;

    void setText(const QString &text);

    [[nodiscard]] QSize padding() const noexcept;

    void setPadding(QSize padding);

    void enableRichText();

protected:
    void changeEvent(QEvent *event) override;
    void paintContent(QPainter &painter) override;

private:
    void scheduleFontSync();
    void syncLabelFont();
    void updatePadding();

    // FontChange arrives from inside QWidgetPrivate::updateFont. Copying the
    // font onto the label from that handler re-enters Qt 6.9's font engine
    // until Qt6Gui overflows the stack. The copy is deferred to the next
    // event-loop turn and skipped when the label already matches.
    bool syncingFont_ = false;
    bool fontSyncQueued_ = false;

    QHBoxLayout layout_;
    QLabel label_;
    QSize padding_;
};

}  // namespace chatterino
