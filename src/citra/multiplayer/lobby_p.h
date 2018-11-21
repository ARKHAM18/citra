// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <utility>
#include <QPixmap>
#include <QStandardItem>
#include <QStandardItemModel>
#include "common/common_types.h"

namespace Column {
enum List {
    EXPAND,
    ROOM_NAME,
    PROGRAM_NAME,
    HOST,
    MEMBER,
    TOTAL,
};
} // namespace Column

class LobbyItem : public QStandardItem {
public:
    LobbyItem() = default;
    explicit LobbyItem(const QString& string) : QStandardItem{string} {}
    virtual ~LobbyItem() override = default;
};

class LobbyItemName : public LobbyItem {
public:
    LobbyItemName() = default;

    explicit LobbyItemName(bool has_password, QString name) : LobbyItem() {
        setData(name, NameRole);
        setData(has_password, PasswordRole);
    }

    QVariant data(int role) const override {
        if (role == Qt::DecorationRole) {
            bool has_password{data(PasswordRole).toBool()};
            return has_password ? QIcon::fromTheme("lock").pixmap(16) : QIcon();
        }
        if (role != Qt::DisplayRole)
            return LobbyItem::data(role);
        return data(NameRole).toString();
    }

    bool operator<(const QStandardItem& other) const override {
        return data(NameRole).toString().localeAwareCompare(other.data(NameRole).toString()) < 0;
    }

    static const int NameRole{Qt::UserRole + 1};
    static const int PasswordRole{Qt::UserRole + 2};
};

class LobbyItemApp : public LobbyItem {
public:
    LobbyItemApp() = default;

    explicit LobbyItemApp(u64 program_id, QString program_name, QPixmap smdh_icon) {
        setData(static_cast<unsigned long long>(program_id), ProgramIDRole);
        setData(program_name, AppNameRole);
        if (!smdh_icon.isNull())
            setData(smdh_icon, IconRole);
    }

    QVariant data(int role) const override {
        if (role == Qt::DecorationRole) {
            auto val{data(IconRole)};
            if (val.isValid())
                val = val.value<QPixmap>().scaled(16, 16, Qt::KeepAspectRatio);
            return val;
        } else if (role != Qt::DisplayRole)
            return LobbyItem::data(role);
        return data(AppNameRole).toString();
    }

    bool operator<(const QStandardItem& other) const override {
        return data(AppNameRole).toString().localeAwareCompare(other.data(AppNameRole).toString()) <
               0;
    }

    static const int ProgramIDRole{Qt::UserRole + 1};
    static const int AppNameRole{Qt::UserRole + 2};
    static const int IconRole{Qt::UserRole + 3};
};

class LobbyItemHost : public LobbyItem {
public:
    LobbyItemHost() = default;

    explicit LobbyItemHost(QString username, QString ip, u16 port) {
        setData(username, HostUsernameRole);
        setData(ip, HostIPRole);
        setData(port, HostPortRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole)
            return LobbyItem::data(role);
        return data(HostUsernameRole).toString();
    }

    bool operator<(const QStandardItem& other) const override {
        return data(HostUsernameRole)
                   .toString()
                   .localeAwareCompare(other.data(HostUsernameRole).toString()) < 0;
    }

    static const int HostUsernameRole{Qt::UserRole + 1};
    static const int HostIPRole{Qt::UserRole + 2};
    static const int HostPortRole{Qt::UserRole + 3};
};

class LobbyMember {
public:
    LobbyMember() = default;
    LobbyMember(const LobbyMember& other) = default;

    explicit LobbyMember(QString username, u64 program_id, QString program_name)
        : username{std::move(username)}, program_id{program_id}, program_name{
                                                                     std::move(program_name)} {}

    ~LobbyMember() = default;

    u64 GetProgramID() const {
        return program_id;
    }

    QString GetUsername() const {
        return username;
    }

    QString GetAppName() const {
        return program_name;
    }

private:
    QString username;
    u64 program_id;
    QString program_name;
};

Q_DECLARE_METATYPE(LobbyMember);

class LobbyItemMemberList : public LobbyItem {
public:
    LobbyItemMemberList() = default;

    explicit LobbyItemMemberList(QList<QVariant> members, u32 max_members) {
        setData(members, MemberListRole);
        setData(max_members, MaxMemberRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole)
            return LobbyItem::data(role);
        auto members = data(MemberListRole).toList();
        return QString("%1 / %2").arg(QString::number(members.size()),
                                      data(MaxMemberRole).toString());
    }

    bool operator<(const QStandardItem& other) const override {
        // Sort by rooms that have the most members
        int left_members{data(MemberListRole).toList().size()};
        int right_members{other.data(MemberListRole).toList().size()};
        return left_members < right_members;
    }

    static const int MemberListRole{Qt::UserRole + 1};
    static const int MaxMemberRole{Qt::UserRole + 2};
};

/// Member information for when a lobby is expanded in the UI
class LobbyItemExpandedMemberList : public LobbyItem {
public:
    LobbyItemExpandedMemberList() = default;

    explicit LobbyItemExpandedMemberList(QList<QVariant> members) {
        setData(members, MemberListRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole)
            return LobbyItem::data(role);
        auto members = data(MemberListRole).toList();
        QString out;
        bool first{true};
        for (const auto& member : members) {
            if (!first)
                out += '\n';
            const auto& m{member.value<LobbyMember>()};
            if (m.GetAppName().isEmpty())
                out += QString("%1 isn't runnning a program").arg(m.GetUsername());
            else
                out += QString("%1 is running %2").arg(m.GetUsername(), m.GetAppName());
            first = false;
        }
        return out;
    }

    static const int MemberListRole{Qt::UserRole + 1};
};
