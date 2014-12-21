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

#include <unistd.h>
#include <signal.h>
#include <linux/input.h>

#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QSettings>

#include "shell.h"
#include "compositor.h"
#include "layer.h"
#include "workspace.h"
#include "shellsurface.h"
#include "seat.h"
#include "binding.h"
#include "view.h"
#include "shellview.h"
#include "output.h"
#include "xwayland.h"
#include "global.h"
#include "pager.h"
#include "dropdown.h"
#include "screenshooter.h"
#include "wlshell/wlshell.h"
#include "desktop-shell/desktop-shell.h"
#include "desktop-shell/desktop-shell-workspace.h"
#include "desktop-shell/desktop-shell-window.h"
#include "effects/zoomeffect.h"
#include "effects/desktopgrid.h"

namespace Orbital {

Shell::Shell(Compositor *c)
     : Object()
     , m_compositor(c)
     , m_grabCursorSetter(nullptr)
     , m_grabCursorUnsetter(nullptr)
     , m_pager(new Pager(c))
{
    initEnvironment();

    addInterface(new XWayland(this));
    addInterface(new WlShell(this, m_compositor));
    addInterface(new DesktopShell(this));
    addInterface(new Dropdown(this));
    addInterface(new Screenshooter(this));

    new ZoomEffect(this);
    new DesktopGrid(this);

    m_focusBinding = c->createButtonBinding(PointerButton::Left, KeyboardModifiers::None);
    m_raiseBinding = c->createButtonBinding(PointerButton::Task, KeyboardModifiers::None);
    m_moveBinding = c->createButtonBinding(PointerButton::Left, KeyboardModifiers::Super);
    m_killBinding = c->createKeyBinding(KEY_ESC, KeyboardModifiers::Super | KeyboardModifiers::Ctrl);
    m_nextWsBinding = c->createKeyBinding(KEY_RIGHT, KeyboardModifiers::Ctrl);
    m_prevWsBinding = c->createKeyBinding(KEY_LEFT, KeyboardModifiers::Ctrl);
    connect(m_focusBinding, &ButtonBinding::triggered, this, &Shell::giveFocus);
    connect(m_raiseBinding, &ButtonBinding::triggered, this, &Shell::raise);
    connect(m_moveBinding, &ButtonBinding::triggered, this, &Shell::moveSurface);
    connect(m_killBinding, &KeyBinding::triggered, this, &Shell::killSurface);
    connect(m_nextWsBinding, &KeyBinding::triggered, this, &Shell::nextWs);
    connect(m_prevWsBinding, &KeyBinding::triggered, this, &Shell::prevWs);

    autostartClients();
}

Shell::~Shell()
{
    qDeleteAll(m_surfaces);
    for (Workspace *w: m_workspaces) {
        delete w;
    }
    delete m_pager;
    delete m_focusBinding;
    delete m_raiseBinding;
    delete m_moveBinding;
}

void Shell::initEnvironment()
{
    if (qEnvironmentVariableIsSet("DBUS_SESSION_BUS_ADDRESS")) {
        return;
    }

    QProcess proc;
    proc.start("dbus-launch", { "--binary-syntax" });
    if (!proc.waitForStarted()) {
        qWarning("Could not start the DBus session.");
        return;
    }
    pid_t pid = proc.processId();
    proc.waitForFinished();
    QByteArray out = proc.readAllStandardOutput();

    setenv("DBUS_SESSION_BUS_ADDRESS", out.constData(), 1);
    setenv("DBUS_SESSION_BUS_PID", qPrintable(QString::number(pid)), 1);
}

static bool shouldAutoStart(const QSettings &settings)
{
    bool hidden = settings.value("Hidden").toBool();
    if (hidden) {
        return false;
    }
    if (settings.contains("OnlyShowIn")) {
        QString onlyShowIn = settings.value("OnlyShowIn").toString();
        for (const QString s: onlyShowIn.split(';')) {
            if (s == "Orbital") {
                return true;
            }
        }
        return false;
    } else if (settings.contains("NotShowIn")) {
        QString notShowIn = settings.value("NotShowIn").toString();
        for (const QString s: notShowIn.split(';')) {
            if (s == "Orbital") {
                return false;
            }
        }
    }
    return true;
}

static void populateAutostartList(QStringList &files, const QString &autostartDir)
{
    QDir dir(autostartDir);
    if (dir.exists()) {
        for (const QFileInfo &fi: dir.entryInfoList({"*.desktop"}, QDir::Files)) {
            QString path = fi.absoluteFilePath();
            QString filename = fi.fileName();
            bool add = true;
            if (!fi.isReadable()) {
                continue;
            }

            for (const QString &p: files) {
                if (QFileInfo(p).fileName() == filename) {
                    add = false;
                    break;
                }
            }
            if (add) {
                files << path;
            }
        }
    }
}

void Shell::autostartClients()
{
    QStringList files;

    QString xdgConfigHome = qgetenv("XDG_CONFIG_HOME");
    if (!xdgConfigHome.isEmpty()) {
        populateAutostartList(files, QString("%1/autostart").arg(xdgConfigHome));
    } else {
        populateAutostartList(files, QString("%1/.config/autostart").arg(QDir::homePath()));
    }

    QString xdgConfigDirs = qgetenv("XDG_CONFIG_DIRS");
    if (!xdgConfigDirs.isEmpty()) {
        for (const QString &d: xdgConfigDirs.split(';')) {
            populateAutostartList(files, QString("%1/autostart").arg(d));
        }
    } else {
        populateAutostartList(files, QString("/etc/xdg/autostart"));
    }

    QDir outputDir = QDir::temp();
    QString dirName = QString("orbital-%1").arg(getpid());
    outputDir.mkdir(dirName);
    outputDir.cd(dirName);

    for (const QString &fi: files) {
        QSettings settings(fi, QSettings::IniFormat);
        settings.beginGroup("Desktop Entry");
        if (!shouldAutoStart(settings)) {
            continue;
        }

        QString exec;
        if (settings.contains("TryExec")) {
            exec = settings.value("TryExec").toString();
        } else {
            exec = settings.value("Exec").toString();
        }
        qDebug("Autostarting '%s'", qPrintable(exec));

        QProcess *proc = new QProcess(this);
        QString bin = exec.split(' ').first();
        proc->setStandardOutputFile(outputDir.filePath(bin));
        proc->setStandardErrorFile(outputDir.filePath(bin));
        proc->start(exec);

        QProcess::startDetached(exec);
        settings.endGroup();
    }
}

Compositor *Shell::compositor() const
{
    return m_compositor;
}

Pager *Shell::pager() const
{
    return m_pager;
}

Workspace *Shell::createWorkspace()
{
    Workspace *ws = new Workspace(this, m_workspaces.count());
    ws->addInterface(new DesktopShellWorkspace(this, ws));
    m_pager->addWorkspace(ws);
    m_workspaces << ws;
    return ws;
}

ShellSurface *Shell::createShellSurface(weston_surface *s)
{
    ShellSurface *surf = new ShellSurface(this, s);
    surf->addInterface(new DesktopShellWindow(findInterface<DesktopShell>()));
    m_surfaces << surf;
    connect(surf, &QObject::destroyed, [this](QObject *o) { m_surfaces.removeOne(static_cast<ShellSurface *>(o)); });
    return surf;
}

QList<Workspace *> Shell::workspaces() const
{
    return m_workspaces;
}

QList<ShellSurface *> Shell::surfaces() const
{
    return m_surfaces;
}

void Shell::setGrabCursor(Pointer *pointer, PointerCursor c)
{
    if (m_grabCursorSetter) {
        m_grabCursorSetter(pointer, c);
    }
}

void Shell::unsetGrabCursor(Pointer *p)
{
    if (m_grabCursorUnsetter) {
        m_grabCursorUnsetter(p);
    }
}

Output *Shell::selectPrimaryOutput(Seat *seat)
{
    struct Out {
        Output *output;
        int vote;
    };
    QList<Out> candidates;

    for (Output *o: compositor()->outputs()) {
        candidates.append({ o, 0 });
    }

    Output *output = nullptr;
    if (candidates.isEmpty()) {
        return nullptr;
    } else if (candidates.size() == 1) {
        output = candidates.first().output;
    } else {
        QList<Seat *> seats;
        if (seat) {
            seats << seat;
        } else {
            seats = compositor()->seats();
        }
        for (Out &o: candidates) {
            for (Seat *s: seats) {
                if (o.output->geometry().contains(s->pointer()->x(), s->pointer()->y())) {
                    o.vote++;
                }
            }
        }
        Out *out = nullptr;
        for (Out &o: candidates) {
            if (!out || out->vote < o.vote) {
                out = &o;
            }
        }
        output = out->output;
    }
    return output;
}

void Shell::lock()
{
    for (Output *o: m_compositor->outputs()) {
        o->lock();
    }
}

void Shell::unlock()
{
    for (Output *o: m_compositor->outputs()) {
        o->unlock();
    }
}

void Shell::configure(ShellSurface *shsurf)
{
    if (!shsurf->isMapped() && !shsurf->workspace()) {
        Output *output = selectPrimaryOutput();
        shsurf->setWorkspace(output->currentWorkspace());

        if (shsurf->type() == ShellSurface::Type::Toplevel) {
            for (Seat *seat: m_compositor->seats()) {
                seat->activate(shsurf);
            }
        }
    }
}

void Shell::setGrabCursorSetter(GrabCursorSetter s)
{
    m_grabCursorSetter = s;
}

void Shell::setGrabCursorUnsetter(GrabCursorUnsetter s)
{
    m_grabCursorUnsetter = s;
}

void Shell::giveFocus(Seat *seat)
{
    if (seat->pointer()->isGrabActive()) {
        return;
    }

    View *focus = m_compositor->pickView(seat->pointer()->x(), seat->pointer()->y());
    if (!focus) {
        return;
    }

    seat->activate(focus->surface());

    // TODO: make this a proper config option
    static bool useSeparateRaise = qgetenv("ORBITAL_SEPARATE_RAISE").toInt();
    if (!useSeparateRaise) {
        ShellSurface *shsurf = qobject_cast<ShellSurface *>(focus->surface());
        if (shsurf && shsurf->isFullscreen()) {
            return;
        }

        if (shsurf) {
            for (Output *o: compositor()->outputs()) {
                ShellView *view = shsurf->viewForOutput(o);
                view->layer()->raiseOnTop(view);
            }
        }
    }
}

void Shell::raise(Seat *seat)
{
    if (seat->pointer()->isGrabActive()) {
        return;
    }

    View *focus = seat->pointer()->focus();
    if (!focus) {
        return;
    }

    ShellSurface *shsurf = qobject_cast<ShellSurface *>(focus->surface());
    if (shsurf && shsurf->isFullscreen()) {
        return;
    }

    if (shsurf) {
        for (Output *o: compositor()->outputs()) {
            ShellView *view = shsurf->viewForOutput(o);
            if (view->layer()->topView() == view) {
                view->layer()->lower(view);
            } else {
                view->layer()->raiseOnTop(view);
            }
        }
    }
}

void Shell::moveSurface(Seat *seat)
{
    if (seat->pointer()->isGrabActive()) {
        return;
    }

    View *focus = seat->pointer()->focus();
    if (!focus) {
        return;
    }

    focus->surface()->move(seat);
}

void Shell::killSurface(Seat *s)
{
    class KillGrab : public PointerGrab
    {
    public:
        void motion(uint32_t time, double x, double y) override
        {
            pointer()->move(x, y);
        }
        void button(uint32_t time, PointerButton button, Pointer::ButtonState state) override
        {
            View *view = pointer()->pickView();
            wl_client *client = view->surface()->client();

            pid_t pid;
            wl_client_get_credentials(client, &pid, NULL, NULL);

            if (pid != getpid()) {
                kill(pid, SIGKILL);
            }
            end();
        }
        void ended() override
        {
            delete abortBinding;
            delete this;
        }
        KeyBinding *abortBinding;
    };

    KillGrab *grab = new KillGrab;
    grab->abortBinding = m_compositor->createKeyBinding(KEY_ESC, KeyboardModifiers::None);
    connect(grab->abortBinding, &KeyBinding::triggered, [grab]() { grab->end(); });
    s->pointer()->setFocus(nullptr);
    grab->start(s, PointerCursor::Kill);
}

void Shell::nextWs(Seat *s)
{
    Output *o = selectPrimaryOutput(s);
    m_pager->activateNextWorkspace(o);
}

void Shell::prevWs(Seat *s)
{
    Output *o = selectPrimaryOutput(s);
    m_pager->activatePrevWorkspace(o);
}

class Client
{
public:
    ~Client() { wl_list_remove(&listener.listener.link); }
    Shell *shell;
    wl_client *client;
    QString interface;
    struct Listener {
        wl_listener listener;
        Client *parent;
    };
    Listener listener;
};

void Shell::addTrustedClient(const QString &interface, wl_client *c)
{
    Client *cl = new Client;
    cl->shell = this;
    cl->client = c;
    cl->interface = interface;
    cl->listener.parent = cl;
    cl->listener.listener.notify = [](wl_listener *l, void *data)
    {
        Client *client = reinterpret_cast<Client::Listener *>(l)->parent;
        client->shell->m_trustedClients[client->interface].removeOne(client);
        delete client;
    };
    wl_client_add_destroy_listener(c, &cl->listener.listener);

    m_trustedClients[interface] << cl;
}

bool Shell::isClientTrusted(const QString &interface, wl_client *c) const
{
    for (Client *cl: m_trustedClients.value(interface)) {
        if (cl->client == c) {
            return true;
        }
    }

    return false;
}

}
