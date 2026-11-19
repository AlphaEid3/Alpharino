// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/buttons/LabelButton.hpp"

#include <QTimer>

namespace chatterino {

LabelButton::LabelButton(const QString &text, BaseWidget *parent, QSize padding)
    : Button(parent)
    , layout_(this)
    , label_(text)
    , padding_(padding)
{
    this->layout_.setContentsMargins(0, 0, 0, 0);
    this->layout_.addWidget(&this->label_);
    this->label_.setAttribute(Qt::WA_TransparentForMouseEvents);
    this->label_.setAlignment(Qt::AlignCenter);
    this->label_.setTextFormat(Qt::PlainText);

    this->updatePadding();
}

QString LabelButton::text() const
{
    return this->label_.text();
}

void LabelButton::setText(const QString &text)
{
    if (this->label_.text() == text)
    {
        return;
    }

    this->label_.setText(text);
}

QSize LabelButton::padding() const noexcept
{
    return this->padding_;
}

void LabelButton::setPadding(QSize padding)
{
    if (this->padding_ == padding)
    {
        return;
    }

    this->padding_ = padding;
    this->updatePadding();
}

void LabelButton::enableRichText()
{
    this->label_.setTextFormat(Qt::RichText);
}

void LabelButton::changeEvent(QEvent *event)
{
    Button::changeEvent(event);

    if (event->type() == QEvent::FontChange && !this->syncingFont_)
    {
        this->scheduleFontSync();
    }
}

void LabelButton::scheduleFontSync()
{
    if (this->fontSyncQueued_)
    {
        return;
    }

    this->fontSyncQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        this->fontSyncQueued_ = false;
        if (this->syncingFont_)
        {
            return;
        }

        this->syncingFont_ = true;
        this->syncLabelFont();
        this->syncingFont_ = false;
    });
}

void LabelButton::paintContent(QPainter &painter)
{
}

void LabelButton::syncLabelFont()
{
    const auto next = this->font();
    const auto current = this->label_.font();
    if (current == next)
    {
        return;
    }

    // QWidget::font() widens the resolve mask, so operator== can fail after a
    // copy that already applied the same face. Another setFont would deliver
    // FontChange again.
    if (current.family() == next.family() &&
        current.pixelSize() == next.pixelSize() &&
        current.pointSizeF() == next.pointSizeF() &&
        current.weight() == next.weight() && current.style() == next.style() &&
        current.hintingPreference() == next.hintingPreference())
    {
        return;
    }

    this->label_.setFont(next);
}

void LabelButton::updatePadding()
{
    auto x = this->padding_.width();
    auto y = this->padding_.height();
    this->label_.setContentsMargins(x, y, x, y);
}

}  // namespace chatterino
