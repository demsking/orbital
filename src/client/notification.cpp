/*
 * Copyright 2014 Giulio Camuffo <giuliocamuffo@gmail.com>
 *
 * This file is part of Orbital
 *
 * Orbital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Orbital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Orbital.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QQuickWindow>
#include <QQuickItem>

#include "notification.h"
#include "client.h"
#include "wayland-desktop-shell-client-protocol.h"

NotificationWindow::NotificationWindow(QObject *p)
                  : QObject(p)
                  , m_window(new QQuickWindow)
                  , m_contentItem(nullptr)
                  , m_inactive(false)
                  , m_surface(nullptr)
{
    m_window->setFlags(Qt::BypassWindowManagerHint);
    m_window->setColor(Qt::transparent);
    m_window->create();
}

NotificationWindow::~NotificationWindow()
{
    notification_surface_destroy(m_surface);
    delete m_window;
}

QQuickItem *NotificationWindow::contentItem() const
{
    return m_contentItem;
}

void NotificationWindow::setContentItem(QQuickItem *item)
{
    if (m_contentItem) {
        return;
    }

    m_contentItem = item;
    if (item) {
        item->setParentItem(m_window->contentItem());
        m_window->setWidth(item->width());
        m_window->setHeight(item->height());
        m_window->show();
        m_surface = Client::client()->pushNotification(m_window, m_inactive);
        connect(item, &QQuickItem::widthChanged, this, &NotificationWindow::resetWidth);
        connect(item, &QQuickItem::heightChanged, this, &NotificationWindow::resetHeight);
    }
}

bool NotificationWindow::inactive() const
{
    return m_inactive;
}

void NotificationWindow::setInactive(bool in)
{
    m_inactive = in;
}

void NotificationWindow::resetWidth()
{
    m_window->setWidth(m_contentItem->width());
}

void NotificationWindow::resetHeight()
{
    m_window->setHeight(m_contentItem->height());
}