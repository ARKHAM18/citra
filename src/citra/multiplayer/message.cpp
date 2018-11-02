// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMessageBox>
#include <QString>
#include "citra/multiplayer/message.h"

namespace NetworkMessage {

const ConnectionError USERNAME_NOT_VALID{
    "Username isn't valid. Must be 4 to 20 alphanumeric characters."};
const ConnectionError ROOMNAME_NOT_VALID{
    "Room name isn't valid. Must be 4 to 20 alphanumeric characters."};
const ConnectionError USERNAME_NOT_VALID_SERVER{
    "Username is already in use or not valid. Please choose another."};
const ConnectionError IP_ADDRESS_NOT_VALID{"IP isn't a valid IPv4 address."};
const ConnectionError PORT_NOT_VALID{"Port must be a number between 0 to 65535."};
const ConnectionError NO_INTERNET{
    "Unable to find an internet connection. Check your internet settings."};
const ConnectionError UNABLE_TO_CONNECT{
    "Unable to connect to the host. Verify that the connection settings are correct. If "
    "you still cannot connect, contact the room host and verify that the host is "
    "properly configured with the external port forwarded."};
const ConnectionError ROOM_IS_FULL{"Unable to connect to the room because it is already full."};
const ConnectionError COULD_NOT_CREATE_ROOM{
    "Creating a room failed. Please retry. Restarting Citra might be necessary."};
const ConnectionError HOST_BANNED{
    "The host of the room has banned you. Speak with the host to unban you "
    "or try a different room."};
const ConnectionError WRONG_VERSION{
    "Version mismatch!\nIf you are connecting to a room created with official/my old builds: "
    "There's no way to do that!\nIf you are connectiong to a room created with my new builds, "
    "please update to the latest version of Citra. If the problem "
    "persists, contact the room host and ask them to update the server."};
const ConnectionError WRONG_PASSWORD{"Incorrect password."};
const ConnectionError GENERIC_ERROR{
    "An unknown error occured. If this error continues to occur, please open an issue"};
const ConnectionError LOST_CONNECTION{"Connection to room lost. Try to reconnect."};
const ConnectionError MAC_COLLISION{"MAC address is already in use. Please choose another."};
const ConnectionError CONSOLE_ID_COLLISION{
    "Your Console ID conflicted with someone else's in the room.\n\nPlease go to Emulation "
    "> Configure > System to regenerate your Console ID."};

static bool WarnMessage(const std::string& title, const std::string& text) {
    return QMessageBox::warning(nullptr, QString::fromStdString(title),
                                QString::fromStdString(text),
                                QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok;
}

void ShowError(const ConnectionError& e) {
    QMessageBox::critical(nullptr, "Error", QString::fromStdString(e.GetString()));
}

bool WarnCloseRoom() {
    return WarnMessage("Leave Room",
                       "You're about to close the room. Any network connections will be closed.");
}

bool WarnDisconnect() {
    return WarnMessage("Disconnect",
                       "You're about to leave the room. Any network connections will be closed.");
}

} // namespace NetworkMessage
