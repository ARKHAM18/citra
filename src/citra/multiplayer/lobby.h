// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>
#include <QFutureWatcher>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include "citra/multiplayer/validation.h"
#include "network/room_member.h"
#include "ui_lobby.h"

namespace Core {
class System;
} // namespace Core

class LobbyModel;
class LobbyFilterProxyModel;

// Listing of all public rooms pulled from API. The lobby should be simple enough for users to
// find the program, and join it.
class Lobby : public QDialog {
    Q_OBJECT

public:
    explicit Lobby(QWidget* parent, Core::System& system);
    ~Lobby() = default;

public slots:
    /**
     * Begin the process to pull the latest room list from API. After the listing is
     * returned from API, `LobbyRefreshed` will be signalled
     */
    void RefreshLobby();

private slots:
    /// Pulls the list of rooms from network and fills out the lobby model with the results
    void OnRefreshLobby();

    /**
     * Handler for single clicking on a room in the list. Expands the treeitem to show member
     * information for the people in the room
     * @param index The row of the proxy model that the user wants to join.
     */
    void OnExpandRoom(const QModelIndex&);

    /**
     * Handler for double clicking on a room in the list. Gathers the host IP address and port and
     * attempts to connect. Will also prompt for a password in case one is required.
     * @param index The row of the proxy model that the user wants to join.
     */
    void OnJoinRoom(const QModelIndex&);

signals:
    void StateChanged(const Network::RoomMember::State&);

private:
    /// Removes all entries in the Lobby before refreshing.
    void ResetModel();

    /**
     * Prompts for a password. Returns an empty QString if the user either did not provide a
     * password or if the user closed the window.
     */
    QString PasswordPrompt();

    QStandardItemModel* model;
    LobbyFilterProxyModel* proxy;

    QFutureWatcher<std::vector<Network::JsonRoom>> room_list_watcher;
    std::unique_ptr<Ui::Lobby> ui;
    QFutureWatcher<void>* watcher;
    Validation validation;
    Core::System& system;
};

// Proxy model for filtering the lobby
class LobbyFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit LobbyFilterProxyModel(QWidget* parent);
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    void sort(int column, Qt::SortOrder order) override;

public slots:
    void SetFilterSearch(const QString&);

private:
    QString filter_search;
};
