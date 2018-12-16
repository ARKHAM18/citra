// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <future>
#include <QColor>
#include <QImage>
#include <QInputDialog>
#include <QList>
#include <QLocale>
#include <QMessageBox>
#include <QMetaType>
#include <QTime>
#include <QtConcurrent/QtConcurrentRun>
#include "citra/main.h"
#include "citra/multiplayer/host_room.h"
#include "citra/multiplayer/message.h"
#include "citra/multiplayer/state.h"
#include "citra/multiplayer/validation.h"
#include "citra/program_list_p.h"
#include "citra/ui_settings.h"
#include "common/logging/log.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/settings.h"
#include "ui_host_room.h"

HostRoomWindow::HostRoomWindow(QWidget* parent, Core::System& system)
    : QDialog{parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint},
      ui{std::make_unique<Ui::HostRoom>()}, system{system} {
    ui->setupUi(this);
    // Set up validation for the text fields
    ui->room_name->setValidator(validation.GetRoomName());
    ui->nickname->setValidator(validation.GetNickname());
    // Disable editing replies text
    ui->tableReplies->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Connect all the widgets to the appropriate events
    connect(ui->buttonAddReply, &QPushButton::released, this, &HostRoomWindow::AddReply);
    connect(ui->buttonRemoveReply, &QPushButton::released, this, &HostRoomWindow::RemoveReply);
    connect(ui->host, &QPushButton::released, this, &HostRoomWindow::Host);
    // Restore the settings
    ui->nickname->setText(UISettings::values.room_nickname);
    ui->room_name->setText(UISettings::values.room_name);
    ui->port->setValue(UISettings::values.room_port);
    int index{static_cast<int>(UISettings::values.host_type)};
    if (index < ui->host_type->count())
        ui->host_type->setCurrentIndex(index);
    ui->room_description->setText(UISettings::values.room_description);
}

HostRoomWindow::~HostRoomWindow() = default;

void HostRoomWindow::Host() {
    if (!ui->nickname->hasAcceptableInput()) {
        NetworkMessage::ShowError(NetworkMessage::NICKNAME_NOT_VALID);
        return;
    }
    if (!ui->room_name->hasAcceptableInput()) {
        NetworkMessage::ShowError(NetworkMessage::ROOMNAME_NOT_VALID);
        return;
    }
    auto& member{system.RoomMember()};
    if (member.GetState() == Network::RoomMember::State::Joining)
        return;
    else if (member.GetState() == Network::RoomMember::State::Joined) {
        auto parent{static_cast<MultiplayerState*>(parentWidget())};
        if (!parent->OnCloseRoom()) {
            close();
            return;
        }
    }
    ui->host->setDisabled(true);
    auto port{ui->port->value()};
    auto password{ui->password->text().toStdString()};
    Network::Room::BanList ban_list;
    if (ui->load_ban_list->isChecked())
        ban_list = UISettings::values.ban_list;
    bool created{system.Room().Create(
        ui->host_type->currentIndex() == 0, ui->room_name->text().toStdString(),
        ui->room_description->toPlainText().toStdString(), ui->nickname->text().toStdString(), port,
        password, ban_list)};
    if (!created) {
        NetworkMessage::ShowError(NetworkMessage::COULD_NOT_CREATE_ROOM);
        LOG_ERROR(Network, "Couldn't create room!");
        ui->host->setEnabled(true);
        return;
    }
    member.Join(ui->nickname->text().toStdString(), Service::CFG::GetConsoleID(system), "127.0.0.1",
                port, BroadcastMac, password);
    // Store settings
    UISettings::values.room_nickname = ui->nickname->text();
    UISettings::values.room_name = ui->room_name->text();
    UISettings::values.host_type = ui->host_type->currentIndex();
    UISettings::values.room_port = ui->port->value();
    UISettings::values.room_description = ui->room_description->toPlainText();
    OnConnection();
}

void HostRoomWindow::AddReply() {
    auto message{QInputDialog::getText(this, "Add Reply", "Message:")};
    if (message.isEmpty())
        return;
    auto parent{static_cast<MultiplayerState*>(parentWidget())};
    const auto& replies{parent->GetReplies()};
    if (replies.find(message.toStdString()) != replies.end()) {
        QMessageBox::critical(this, "Error", "A reply with this message already exists.");
        return;
    }
    auto reply{QInputDialog::getText(this, "Add Reply", "Reply:")};
    if (reply.isEmpty())
        return;
    int row{ui->tableReplies->rowCount()};
    ui->tableReplies->insertRow(row);
    ui->tableReplies->setItem(row, 0 /* Message */, new QTableWidgetItem(message));
    ui->tableReplies->setItem(row, 1 /* Reply */, new QTableWidgetItem(reply));
    ui->buttonRemoveReply->setEnabled(true);
    UpdateReplies();
}

void HostRoomWindow::RemoveReply() {
    for (const auto& item : ui->tableReplies->selectedItems())
        ui->tableReplies->removeRow(item->row());
    UpdateReplies();
    if (ui->tableReplies->rowCount() == 0)
        ui->buttonRemoveReply->setEnabled(false);
}

void HostRoomWindow::UpdateReplies() {
    // Only update replies if the user is hosting a room.
    if (system.Room().IsOpen()) {
        auto parent{static_cast<MultiplayerState*>(parentWidget())};
        MultiplayerState::Replies replies;
        for (int i{}; i < ui->tableReplies->rowCount(); ++i)
            replies.emplace(ui->tableReplies->item(i, /* Message */ 0)->text().toStdString(),
                            ui->tableReplies->item(i, /* Reply */ 1)->text().toStdString());
        parent->SetReplies(replies);
    }
}

void HostRoomWindow::OnConnection() {
    ui->host->setEnabled(true);
    if (system.RoomMember().GetState() == Network::RoomMember::State::Joining) {
        UpdateReplies();
        close();
    }
}
