/*
 * Copyright 2013-2014 Giulio Camuffo <giuliocamuffo@gmail.com>
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

#include <linux/input.h>

#include <QDebug>

#include "../shell.h"
#include "../compositor.h"
#include "../binding.h"
#include "../global.h"
#include "../seat.h"
#include "../transform.h"
#include "../workspace.h"
#include "../output.h"
#include "../view.h"
#include "../pager.h"
#include "desktopgrid.h"

namespace Orbital {


class DesktopGrid::WsState
{
public:
    QHash<WorkspaceView *, Transform> originalTransforms;

};

class DesktopGrid::Grab : public PointerGrab
{
public:
    void focus() override
    {
        foreach (Output *out, desktopgrid->m_activeOutputs.keys()) {
            if (out->contains(pointer()->x(), pointer()->y())) {
                return;
            }
        }
        end();
    }
    void motion(uint32_t time, double x, double y) override
    {
        pointer()->move(x, y);
    }
    void button(uint32_t time, PointerButton button, Pointer::ButtonState state) override
    {
        if (state == Pointer::ButtonState::Released) {
            View *view = pointer()->pickView();
            Output *out = view->output();
            if (!out) {
                return;
            }
            foreach (Workspace *ws, shell->workspaces()) {
                WorkspaceView *wsv = ws->viewForOutput(out);
                if (wsv->ownsView(view)) {
                    desktopgrid->terminate(out, ws);
                    break;
                }
            }
        }
    }
    void ended() override
    {
        delete this;
    }

    Shell *shell;
    DesktopGrid *desktopgrid;
};

DesktopGrid::DesktopGrid(Shell *shell)
           : Effect(shell)
           , m_shell(shell)
{
    m_binding = shell->compositor()->createKeyBinding(KEY_G, KeyboardModifiers::Super);
    connect(m_binding, &KeyBinding::triggered, this, &DesktopGrid::run);

    Compositor *c = m_shell->compositor();
    connect(c, &Compositor::outputCreated, this, &DesktopGrid::outputCreated);
    connect(c, &Compositor::outputRemoved, this, &DesktopGrid::outputRemoved);
    foreach (Output *out, c->outputs()) {
        connect(out, &Output::pointerEnter, this, &DesktopGrid::pointerEnter);
    }
}

DesktopGrid::~DesktopGrid()
{
}

void DesktopGrid::run(Seat *seat, uint32_t time, int key)
{
    Output *out = m_shell->selectPrimaryOutput(seat);

    int numWs = m_shell->workspaces().count();

    if (m_activeOutputs.contains(out)) {
        terminate(out, nullptr);
    } else {
        WsState *state = new WsState;
        m_activeOutputs.insert(out, state);

        const int margin_w = out->width() / 20;
        const int margin_h = out->height() / 20;

        QRect fullRect;
        foreach (Workspace *w, m_shell->workspaces()) {
            WorkspaceView *wsv = w->viewForOutput(out);
            QPoint p = wsv->logicalPos();
            p.rx() = (p.x() + 2) * margin_w + p.x() * out->geometry().width();
            p.ry() = (p.y() + 2) * margin_h + p.y() * out->geometry().height();
            QRect rect = QRect(p, out->geometry().size());
            fullRect |= rect;
        }
        fullRect.setTopLeft(QPoint());

        double rx = (double)out->width() / (double)fullRect.width();
        double ry = (double)out->height() / (double)fullRect.height();

        QSize fullSize;
        if (rx > ry) {
            rx = ry;
            double ratio = (double)out->width() / (double)out->height();
            fullSize = QSize(fullRect.height() * ratio, fullRect.height());
        } else {
            double ratio = (double)out->height() / (double)out->width();
            fullSize = QSize(fullRect.width(), fullRect.width() * ratio);
            ry = rx;
        }

        int margin_x = (fullSize.width() - fullRect.width()) / 2. * rx;
        int margin_y = (fullSize.height() - fullRect.height()) / 2. * rx;

        double width = rx * out->width();
        double height = ry * out->height();

        for (int i = 0; i < numWs; ++i) {
            Workspace *w = m_shell->workspaces().at(i);
            WorkspaceView *wsv = w->viewForOutput(out);

            QPoint p = wsv->logicalPos();
            p.rx() = (p.x() * out->width() + (p.x() + 1) * margin_w) * rx;
            p.ry() = (p.y() * out->height() + (p.y() + 1) * margin_h) * rx;
            int x = p.x() - wsv->pos().x() + margin_x;
            int y = p.y() - wsv->pos().y() + margin_y;

            Transform tr;
            tr.scale(rx, rx);
            tr.translate(x, y);

            state->originalTransforms.insert(wsv, wsv->transform());

            wsv->setTransform(tr);
            wsv->setMask(QRect(p.x() + out->x(), y + out->y(), width, height));
        }

        Grab *grab = new Grab;
        grab->shell = m_shell;
        grab->desktopgrid = this;
        grab->start(seat, PointerCursor::Arrow);
    }
}

void DesktopGrid::terminate(Output *out, Workspace *ws)
{
    WsState *state = m_activeOutputs.take(out);
    int numWs = m_shell->workspaces().count();
    for (int i = 0; i < numWs; ++i) {
        Workspace *w = m_shell->workspaces().at(i);

        WorkspaceView *wsv = w->viewForOutput(out);
        wsv->setTransform(state->originalTransforms.value(wsv));
        wsv->resetMask();
    }
    delete state;
    if (ws) {
        m_shell->pager()->activate(ws, out);
    }
}

void DesktopGrid::outputCreated(Output *o)
{
    connect(o, &Output::pointerEnter, this, &DesktopGrid::pointerEnter);
}

void DesktopGrid::outputRemoved(Output *o)
{
    if (m_activeOutputs.contains(o)) {
        delete m_activeOutputs.take(o);
    }
}

void DesktopGrid::pointerEnter(Pointer *p)
{
    Output *out = static_cast<Output *>(sender());
    if (m_activeOutputs.contains(out)) {
        Grab *grab = new Grab;
        grab->shell = m_shell;
        grab->desktopgrid = this;
        grab->start(p->seat(), PointerCursor::Arrow);
    }
}

}
