// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTimer>
#include <QVariant>
#include "citra_qt/multiplayer/chat_room.h"
#include "citra_qt/multiplayer/validation.h"
#include "network/network.h"

namespace Ui {
class HostRoom;
} // namespace Ui

class ConnectionError;
class ComboBoxProxyModel;

class ChatMessage;

class HostRoomWindow : public QDialog {
    Q_OBJECT

public:
    explicit HostRoomWindow(QWidget* parent, QStandardItemModel* list);
    ~HostRoomWindow();

private slots:
    /**
     * Handler for connection status changes. Launches the chat window if successful or
     * displays an error
     */
    void OnConnection();

private:
    void Host();
    void Stop();

    QStandardItemModel* game_list;
    ComboBoxProxyModel* proxy;
    std::unique_ptr<Ui::HostRoom> ui;
    Validation validation;
    QTimer timer;
};

/**
 * Proxy Model for the game list combo box so we can reuse the game list model while still
 * displaying the fields slightly differently
 */
class ComboBoxProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    int columnCount(const QModelIndex& idx) const override {
        return 1;
    }

    QVariant data(const QModelIndex& idx, int role) const override;

    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
};
