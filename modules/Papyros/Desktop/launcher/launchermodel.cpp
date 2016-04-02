/*
 * QML Desktop - Set of tools written in C++ for QML
 *
 * Copyright (C) 2015-2016 Michael Spencer <sonrisesoftware@gmail.com>
 *               2015 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QtGui/QIcon>
#include <QDebug>

#include <KConfigCore/KConfigGroup>

#include "launchermodel.h"
#include "application.h"

LauncherModel::LauncherModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_config = KSharedConfig::openConfig("papyros-shell", KConfig::NoGlobals);
}

LauncherModel::~LauncherModel()
{
    // Delete the items
    while (!m_list.isEmpty())
        m_list.takeFirst()->deleteLater();
}

void LauncherModel::setIncludePinnedApplications(bool includePinnedApps)
{
    if (m_includePinnedApps == includePinnedApps)
        return;

    if (m_list.count() > 0)
        return;

    if (includePinnedApps) {
        // Add pinned launchers
        const QStringList pinnedLaunchers = m_config->group("appshelf")
                .readEntry("pinnedApps", defaultPinnedApps());
        beginInsertRows(QModelIndex(), 0, m_list.size());

        for (QString appId : pinnedLaunchers) {
            m_list.append(new Application(appId, true, this));
        }

        endInsertRows();
    }

    emit includePinnedApplicationsChanged();
}

bool LauncherModel::includePinnedApplications() const
{
    return m_includePinnedApps;
}

void LauncherModel::setApplicationManager(ApplicationManager *applicationManager)
{
    if (m_applicationManager == applicationManager)
        return;

    if (m_applicationManager != nullptr) {
        disconnect(m_applicationManager, &ApplicationManager::applicationAdded,
                   this, &LauncherModel::handleApplicationAdded);
        disconnect(m_applicationManager, &ApplicationManager::applicationRemoved,
                   this, &LauncherModel::handleApplicationRemoved);
        disconnect(m_applicationManager, &ApplicationManager::applicationFocused,
                   this, &LauncherModel::handleApplicationFocused);
        disconnect(m_applicationManager, &ApplicationManager::applicationUnfocused,
                   this, &LauncherModel::handleApplicationUnfocused);
    }

    m_applicationManager = applicationManager;

    emit applicationManagerChanged();

    if (applicationManager != nullptr) {
        connect(applicationManager, &ApplicationManager::applicationAdded,
                this, &LauncherModel::handleApplicationAdded);
        connect(applicationManager, &ApplicationManager::applicationRemoved,
                this, &LauncherModel::handleApplicationRemoved);
        connect(applicationManager, &ApplicationManager::applicationFocused,
                this, &LauncherModel::handleApplicationFocused);
        connect(applicationManager, &ApplicationManager::applicationUnfocused,
                this, &LauncherModel::handleApplicationUnfocused);
    }
}

ApplicationManager *LauncherModel::applicationManager() const
{
    return m_applicationManager;
}

void LauncherModel::handleApplicationAdded(const QString &appId, pid_t pid)
{
    // Do we have already an icon?
    for (int i = 0; i < m_list.size(); i++) {
        Application *app = m_list.at(i);
        if (app->appId() == appId) {
            app->m_pids.insert(pid);
            app->setState(Application::Running);
            QModelIndex modelIndex = index(i);
            emit dataChanged(modelIndex, modelIndex);
            return;
        }
    }

    // Otherwise create one
    beginInsertRows(QModelIndex(), m_list.size(), m_list.size());
    Application *app = new Application(appId, this);
    app->setState(Application::Running);
    app->m_pids.insert(pid);
    m_list.append(app);
    endInsertRows();
}

void LauncherModel::handleApplicationRemoved(const QString &appId, pid_t pid)
{
    for (int i = 0; i < m_list.size(); i++) {
        Application *app = m_list.at(i);
        if (app->appId() == appId) {
            // Remove this pid and determine if there are any processes left
            app->m_pids.remove(pid);
            if (app->m_pids.count() > 0)
                return;

            if (app->isPinned() && m_includePinnedApps) {
                // If it's pinned we just unset the flags if all pids are gone
                app->setState(Application::NotRunning);
                app->setFocused(false);
                QModelIndex modelIndex = index(i);
                emit dataChanged(modelIndex, modelIndex);
            } else {
                // Otherwise the icon goes away because it wasn't meant
                // to stay
                beginRemoveRows(QModelIndex(), i, i);
                m_list.takeAt(i)->deleteLater();
                endRemoveRows();
            }
            break;
        }
    }
}

void LauncherModel::handleApplicationFocused(const QString &appId)
{
    for (int i = 0; i < m_list.size(); i++) {
        Application *app = m_list.at(i);
        if (app->appId() == appId) {
            app->setFocused(true);
            QModelIndex modelIndex = index(i);
            emit dataChanged(modelIndex, modelIndex);

            if (!m_includePinnedApps) {
                moveRows(i, 1, 0);
            }

            break;
        }
    }
}

void LauncherModel::handleApplicationUnfocused(const QString &appId)
{
    for (int i = 0; i < m_list.size(); i++) {
        Application *app = m_list.at(i);
        if (app->appId() == appId) {
            app->setFocused(false);
            QModelIndex modelIndex = index(i);
            Q_EMIT dataChanged(modelIndex, modelIndex);
            break;
        }
    }
}

QStringList LauncherModel::defaultPinnedApps() {
    QStringList defaultApps;
    defaultApps << "org.gnome.Dictionary";

    return defaultApps;
}

QHash<int, QByteArray> LauncherModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(AppIdRole, "appId");
    roles.insert(DesktopFileRole, "desktopFile");
    roles.insert(ActionsRole, "actions");
    roles.insert(StateRole, "state");
    roles.insert(RunningRole, "running");
    roles.insert(FocusedRole, "focused");
    roles.insert(PinnedRole, "pinned");
    return roles;
}

int LauncherModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_list.size();
}

QVariant LauncherModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    Application *item = m_list.at(index.row());

    switch (role) {
    case Qt::DecorationRole:
        return QIcon::fromTheme(item->desktopFile()->iconName());
    case Qt::DisplayRole:
        return item->desktopFile()->name();
    case AppIdRole:
        return item->appId();
    case DesktopFileRole:
        return QVariant::fromValue(item->desktopFile());
    case PinnedRole:
        return item->isPinned();
    case RunningRole:
        return item->isRunning();
    case FocusedRole:
        return item->isFocused();
    default:
        break;
    }

    return QVariant();
}

Application *LauncherModel::get(int index) const
{
    if (index < 0 || index >= m_list.size())
        return Q_NULLPTR;
    return m_list.at(index);
}

int LauncherModel::indexFromAppId(const QString &appId) const
{
    for (int i = 0; i < m_list.size(); i++) {
        if (m_list.at(i)->appId() == appId)
            return 0;
    }

    return -1;
}

void LauncherModel::pin(const QString &appId)
{
    if (!m_includePinnedApps)
        return;

    Application *found = Q_NULLPTR;

    int pinAtIndex = 0;

    Q_FOREACH (Application *item, m_list) {
        if (item->isPinned())
            pinAtIndex++;

        if (item->appId() != appId)
            continue;

        found = item;
        break;
    }

    if (!found)
        return;

    Q_ASSERT(!found->isPinned());

    found->setPinned(true);

    int foundIndex = m_list.indexOf(found);

    if (foundIndex != pinAtIndex) {
        moveRows(foundIndex, 1, pinAtIndex);
    } else {
        QModelIndex modelIndex = index(foundIndex);
        Q_EMIT dataChanged(modelIndex, modelIndex);
    }

    pinLauncher(appId, true);
}

void LauncherModel::unpin(const QString &appId)
{
    if (!m_includePinnedApps)
        return;

    Application *found = Q_NULLPTR;

    Q_FOREACH (Application *item, m_list) {
        if (!item->isPinned())
            break;

        if (item->appId() != appId)
            continue;

        found = item;
    }

    if (!found)
        return;

    Q_ASSERT(found->isPinned());

    int i = m_list.indexOf(found);

    // Remove the item when unpinned and not running
    if (found->isRunning()) {
        found->setPinned(false);
        moveRows(i, 1, m_list.size() - 1);
    } else {
        beginRemoveRows(QModelIndex(), i, i);
        m_list.takeAt(i)->deleteLater();
        endRemoveRows();
    }

    pinLauncher(appId, false);
}

void LauncherModel::pinLauncher(const QString &appId, bool pinned)
{
    KConfigGroup group = m_config->group("appshelf");

    // Currently pinned launchers
    QStringList pinnedLaunchers = m_config->group("appshelf")
            .readEntry("pinnedApps", defaultPinnedApps());

    // Add or remove from the pinned launchers
    if (pinned)
         pinnedLaunchers.append(appId);
    else
         pinnedLaunchers.removeOne(appId);

    group.writeEntry("pinnedApps", pinnedLaunchers);
    group.sync();
}

bool LauncherModel::moveRows(int sourceRow, int count, int destinationChild) {
    return moveRows(QModelIndex(), sourceRow, count, QModelIndex(), destinationChild);
}

bool LauncherModel::moveRows(const QModelIndex & sourceParent, int sourceRow, int count, const QModelIndex & destinationParent, int destinationChild) {
    QList<Application *> tmp;

    Q_UNUSED(sourceParent);
    Q_UNUSED(destinationParent);

    if (sourceRow + count - 1 < destinationChild) {
        beginMoveRows(QModelIndex(), sourceRow, sourceRow + count - 1, QModelIndex(), destinationChild + 1);
        for (int i = sourceRow; i < sourceRow + count; i++) {
            Q_ASSERT(m_list[i]);
            tmp << m_list.takeAt(i);
        }
        for (int i = 0; i < count; i++) {
            Q_ASSERT(tmp[i]);
            m_list.insert(destinationChild - count + 2 + i,tmp[i]);
        }
        endMoveRows();
    } else if (sourceRow > destinationChild) {
        beginMoveRows(QModelIndex(), sourceRow, sourceRow + count - 1, QModelIndex(), destinationChild);
        for (int i = sourceRow; i < sourceRow + count; i++) {
            Q_ASSERT(m_list[i]);
            tmp << m_list.takeAt(i);
        }
        for (int i = 0; i < count; i++) {
            Q_ASSERT(tmp[i]);
            m_list.insert(destinationChild + i,tmp[i]);
        }
        endMoveRows();
    }
    return true;
}
