// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QInputDialog>
#include <QList>
#include <QtConcurrent/QtConcurrentRun>
#include "citra/main.h"
#include "citra/multiplayer/client_room.h"
#include "citra/multiplayer/lobby.h"
#include "citra/multiplayer/lobby_p.h"
#include "citra/multiplayer/message.h"
#include "citra/multiplayer/state.h"
#include "citra/multiplayer/validation.h"
#include "citra/program_list_p.h"
#include "citra/ui_settings.h"
#include "common/logging/log.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/settings.h"
#include "network/room_member.h"

Lobby::Lobby(QWidget* parent, Core::System& system)
    : QDialog{parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint},
      ui{std::make_unique<Ui::Lobby>()}, system{system} {
    ui->setupUi(this);
    // Setup the watcher for background connections
    watcher = new QFutureWatcher<void>;
    model = new QStandardItemModel(ui->room_list);
    // Create a proxy for filtering
    proxy = new LobbyFilterProxyModel(this);
    proxy->setSourceModel(model);
    proxy->setDynamicSortFilter(true);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setSortLocaleAware(true);
    ui->room_list->setModel(proxy);
    ui->room_list->header()->setSectionResizeMode(QHeaderView::Interactive);
    ui->room_list->header()->stretchLastSection();
    ui->room_list->setAlternatingRowColors(true);
    ui->room_list->setSelectionMode(QHeaderView::SingleSelection);
    ui->room_list->setSelectionBehavior(QHeaderView::SelectRows);
    ui->room_list->setVerticalScrollMode(QHeaderView::ScrollPerPixel);
    ui->room_list->setHorizontalScrollMode(QHeaderView::ScrollPerPixel);
    ui->room_list->setSortingEnabled(true);
    ui->room_list->setEditTriggers(QHeaderView::NoEditTriggers);
    ui->room_list->setExpandsOnDoubleClick(false);
    ui->room_list->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->nickname->setValidator(validation.GetNickname());
    ui->nickname->setText(UISettings::values.lobby_nickname);
    // UI Buttons
    connect(ui->refresh_list, &QPushButton::released, this, &Lobby::RefreshLobby);
    connect(ui->search, &QLineEdit::textChanged, proxy, &LobbyFilterProxyModel::SetFilterSearch);
    connect(ui->room_list, &QTreeView::doubleClicked, this, &Lobby::OnJoinRoom);
    connect(ui->room_list, &QTreeView::clicked, this, &Lobby::OnExpandRoom);
    // Actions
    connect(&room_list_watcher, &QFutureWatcher<std::vector<Network::JsonRoom>>::finished, this,
            &Lobby::OnRefreshLobby);
    // Manually start a refresh when the window is opening
    // TODO: if this refresh is slow for people with bad internet, then don't do it as
    // part of the constructor, but offload the refresh until after the window shown. perhaps emit a
    // refreshroomlist signal from places that open the lobby
    RefreshLobby();
}

QString Lobby::PasswordPrompt() {
    bool ok{};
    const auto text{QInputDialog::getText(this, "Password Required",
                                          "Password:", QLineEdit::Password, "", &ok)};
    return ok ? text : QString();
}

void Lobby::OnExpandRoom(const QModelIndex& index) {
    auto member_index{proxy->index(index.row(), Column::MEMBER)};
    auto member_list{proxy->data(member_index, LobbyItemMemberList::MemberListRole).toList()};
}

void Lobby::OnJoinRoom(const QModelIndex& source) {
    // Prevent the user from trying to join a room while they are already joining.
    auto& member{system.RoomMember()};
    if (member.GetState() == Network::RoomMember::State::Joining)
        return;
    else if (member.GetState() == Network::RoomMember::State::Joined)
        // And ask if they want to leave the room if they are already in one.
        if (!NetworkMessage::WarnDisconnect())
            return;
    auto index{source};
    // If the user double clicks on a child row (aka the member list) then use the parent instead
    if (source.parent() != QModelIndex())
        index = source.parent();
    if (!ui->nickname->hasAcceptableInput()) {
        NetworkMessage::ShowError(NetworkMessage::NICKNAME_NOT_VALID);
        return;
    }
    // Get a password to pass if the room is password protected
    auto password_index{proxy->index(index.row(), Column::ROOM_NAME)};
    bool has_password{proxy->data(password_index, LobbyItemName::PasswordRole).toBool()};
    const auto password{has_password ? PasswordPrompt().toStdString() : ""};
    if (has_password && password.empty())
        return;
    auto connection_index{proxy->index(index.row(), Column::HOST)};
    const auto nickname{ui->nickname->text().toStdString()};
    const auto ip{
        proxy->data(connection_index, LobbyItemHost::HostIPRole).toString().toStdString()};
    int port{proxy->data(connection_index, LobbyItemHost::HostPortRole).toInt()};
    // Attempt to connect in a different thread
    auto f{QtConcurrent::run([this, nickname, ip, port, password] {
        system.RoomMember().Join(nickname, Service::CFG::GetConsoleID(system), ip.c_str(), port,
                                 BroadcastMac, password);
    })};
    watcher->setFuture(f);
    // TODO: disable widgets and display a connecting while we wait
    // Save settings
    UISettings::values.lobby_nickname = ui->nickname->text();
    UISettings::values.ip = proxy->data(connection_index, LobbyItemHost::HostIPRole).toString();
    UISettings::values.port = proxy->data(connection_index, LobbyItemHost::HostPortRole).toUInt();
}

void Lobby::ResetModel() {
    model->clear();
    model->insertColumns(0, Column::TOTAL);
    model->setHeaderData(Column::EXPAND, Qt::Horizontal, "", Qt::DisplayRole);
    model->setHeaderData(Column::ROOM_NAME, Qt::Horizontal, "Room Name", Qt::DisplayRole);
    model->setHeaderData(Column::HOST, Qt::Horizontal, "Host", Qt::DisplayRole);
    model->setHeaderData(Column::MEMBER, Qt::Horizontal, "Members", Qt::DisplayRole);
}

void Lobby::RefreshLobby() {
    ResetModel();
    ui->refresh_list->setEnabled(false);
    ui->refresh_list->setText("Refreshing");
    room_list_watcher.setFuture(
        QtConcurrent::run([this]() { return system.Room().GetRoomList(); }));
    // TODO: Display an error box about announce couldn't be started
}

void Lobby::OnRefreshLobby() {
    auto new_room_list{room_list_watcher.result()};
    for (auto room : new_room_list) {
        QList<QVariant> members;
        for (auto member : room.members) {
            QVariant var;
            var.setValue(LobbyMember{QString::fromStdString(member.nickname),
                                     QString::fromStdString(member.program)});
            members.append(var);
        }
        auto first_item{new LobbyItem()};
        QList<QStandardItem*> row{
            first_item,
            new LobbyItemName(room.has_password, QString::fromStdString(room.name)),
            new LobbyItemHost(QString::fromStdString(room.creator), QString::fromStdString(room.ip),
                              room.port),
            new LobbyItemMemberList(members),
        };
        model->appendRow(row);
        // To make the rows expandable, add the member data as a child of the first column of the
        // rows with people in them and have Qt set them to colspan after the model is finished
        // resetting
        if (!room.description.empty())
            first_item->appendRow(
                new LobbyItemDescription(QString::fromStdString(room.description)));
        if (room.members.size() > 0)
            first_item->appendRow(new LobbyItemExpandedMemberList(members));
    }
    // Reenable the refresh button and resize the columns
    ui->refresh_list->setEnabled(true);
    ui->refresh_list->setText("Refresh List");
    ui->room_list->header()->stretchLastSection();
    for (int i{}; i < Column::TOTAL - 1; ++i)
        ui->room_list->resizeColumnToContents(i);
    // Set the member list child items to span all columns
    for (int i{}; i < proxy->rowCount(); i++) {
        auto parent{model->item(i, 0)};
        for (int j{}; j < parent->rowCount(); j++)
            ui->room_list->setFirstColumnSpanned(j, proxy->index(i, 0), true);
    }
}

LobbyFilterProxyModel::LobbyFilterProxyModel(QWidget* parent) : QSortFilterProxyModel{parent} {}

bool LobbyFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    // Prioritize filters by fastest to compute
    // Pass over any child rows (aka row that shows the members in the room)
    if (sourceParent != QModelIndex())
        return true;
    // Filter by search parameters
    if (!filter_search.isEmpty()) {
        auto room_name{sourceModel()->index(sourceRow, Column::ROOM_NAME, sourceParent)};
        auto host_name{sourceModel()->index(sourceRow, Column::HOST, sourceParent)};
        bool room_name_match{sourceModel()
                                 ->data(room_name, LobbyItemName::NameRole)
                                 .toString()
                                 .contains(filter_search, filterCaseSensitivity())};
        bool nickname_match{sourceModel()
                                ->data(host_name, LobbyItemHost::HostCreatorRole)
                                .toString()
                                .contains(filter_search, filterCaseSensitivity())};
        if (!room_name_match && !nickname_match)
            return false;
    }
    return true;
}

void LobbyFilterProxyModel::sort(int column, Qt::SortOrder order) {
    sourceModel()->sort(column, order);
}

void LobbyFilterProxyModel::SetFilterSearch(const QString& filter) {
    filter_search = filter;
    invalidate();
}
