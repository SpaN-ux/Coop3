// The Discord application CoopVC shows up as in a player's profile.
//
// The number is the Application ID of the application called "CoopVC" in
// Discord's developer portal (discord.com/developers/applications, General
// Information). Its Rich Presence art assets hold one image under the key
// "logo". Zero would leave the presence off. CoopVC.ini's `discordAppId`
// overrides it (presence.h).
#pragma once

#include <cstdint>

namespace coopvc {

constexpr uint64_t DISCORD_APP_ID = 1555279137592311990ull;

} // namespace coopvc
