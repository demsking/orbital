/*
 * Copyright 2013 Giulio Camuffo <giuliocamuffo@gmail.com>
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

#include "workspace.h"
#include "wayland-desktop-shell-client-protocol.h"
#include "utils.h"

Workspace::Workspace(desktop_shell_workspace *ws, QObject *p)
         : QObject(p)
         , m_workspace(ws)
         , m_active(false)
{
    desktop_shell_workspace_add_listener(ws, &m_workspace_listener, this);
}

Workspace::~Workspace()
{
    desktop_shell_workspace_remove(m_workspace);
    desktop_shell_workspace_destroy(m_workspace);
}

void Workspace::handleActivated(desktop_shell_workspace *ws)
{
    m_active = true;
    emit activeChanged();
}

void Workspace::handleDeactivated(desktop_shell_workspace *ws)
{
    m_active = false;
    emit activeChanged();
}

const desktop_shell_workspace_listener Workspace::m_workspace_listener = {
    wrapInterface(&Workspace::handleActivated),
    wrapInterface(&Workspace::handleDeactivated)
};
