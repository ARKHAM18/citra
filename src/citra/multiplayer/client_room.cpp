// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <future>
#include <QColor>
#include <QImage>
#include <QList>
#include <QLocale>
#include <QMetaType>
#include <QTime>
#include <QtConcurrent/QtConcurrentRun>
#include "citra/multiplayer/client_room.h"
#include "citra/multiplayer/message.h"
#include "citra/multiplayer/state.h"
#include "citra/program_list_p.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "ui_client_room.h"

ClientRoomWindow::ClientRoomWindow(QWidget* parent, Core::System& system)
    : QDialog{parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint},
      ui{std::make_unique<Ui::ClientRoom>()}, system{system} {
    ui->setupUi(this);
    // Setup the callbacks for network updates
    auto& member{system.RoomMember()};
    member.BindOnRoomInformationChanged(
        [this](const Network::RoomInformation& info) { emit RoomInformationChanged(info); });
    member.BindOnStateChanged(
        [this](const Network::RoomMember::State& state) { emit StateChanged(state); });
    connect(this, &ClientRoomWindow::RoomInformationChanged, this, &ClientRoomWindow::OnRoomUpdate);
    connect(this, &ClientRoomWindow::StateChanged, this, &ClientRoomWindow::OnStateChange);
    // Update the state
    OnStateChange(member.GetState());
    connect(ui->disconnect, &QPushButton::released, [this] { Disconnect(); });
    ui->disconnect->setDefault(false);
    ui->disconnect->setAutoDefault(false);
    UpdateView();
    connect(ui->chat, &ChatRoom::Pinged, this, &ClientRoomWindow::ShowNotification);
}

ClientRoomWindow::~ClientRoomWindow() = default;

void ClientRoomWindow::SetModPerms(bool is_mod) {
    ui->chat->SetModPerms(is_mod);
}

void ClientRoomWindow::OnRoomUpdate(const Network::RoomInformation& info) {
    UpdateView();
}

void ClientRoomWindow::OnStateChange(const Network::RoomMember::State& state) {
    if (state == Network::RoomMember::State::Joined) {
        ui->chat->Clear();
        ui->chat->AppendStatusMessage("Connected");
    }
    UpdateView();
}

void ClientRoomWindow::Disconnect(bool confirm) {
    auto parent{static_cast<MultiplayerState*>(parentWidget())};
    if (parent->OnCloseRoom(confirm)) {
        ui->chat->AppendStatusMessage("Disconnected");
        close();
    }
}

void ClientRoomWindow::UpdateView() {
    auto& member{system.RoomMember()};
    if (member.IsConnected()) {
        ui->chat->Enable();
        ui->disconnect->setEnabled(true);
        auto member_list{member.GetMemberInformation()};
        ui->chat->SetMemberList(member_list);
        const auto information{member.GetRoomInformation()};
        setWindowTitle(QString("%1 (%2 members) - connected")
                           .arg(QString::fromStdString(information.name))
                           .arg(member_list.size()));
        ui->description->setText(QString::fromStdString(information.description));
        return;
    }
}
