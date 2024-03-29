////////////////////////////////////////////////////////////////////////////////////////
// Copyright(C) 1999 - 2005 Id Software, Inc.
// Copyright(C) 2000 - 2006 Tim Angus
// Copyright(C) 2011 - 2023 Dusan Jocic <dusanjocic@msn.com>
//
// This file is part of OpenWolf.
//
// OpenWolf is free software; you can redistribute it
// and / or modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 3 of the License,
// or (at your option) any later version.
//
// OpenWolf is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenWolf; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110 - 1301  USA
//
// -------------------------------------------------------------------------------------
// File name:   sgame_team.cpp
// Created:
// Compilers:   Microsoft (R) C/C++ Optimizing Compiler Version 19.26.28806 for x64,
//              gcc (Ubuntu 9.3.0-10ubuntu2) 9.3.0,
//              AppleClang 9.0.0.9000039
// Description:
// -------------------------------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////////////

#include <sgame/sgame_precompiled.hpp>

/*
===============
idSGameTeam::idSGameTeam
===============
*/
idSGameTeam::idSGameTeam(void) {
}

/*
===============
idSGameTeam::~idSGameTeam
===============
*/
idSGameTeam::~idSGameTeam(void) {
}

/*
==============
idSGameTeam::PrintMsg

nullptr for everyone
==============
*/
void idSGameTeam::PrintMsg(gentity_t *ent, pointer fmt, ...) {
    valueType msg[ 1024 ];
    va_list argptr;
    valueType *p;

    va_start(argptr, fmt);

    if(Q_vsprintf_s(msg, sizeof(msg), sizeof(msg), fmt,
                    argptr) > sizeof(msg)) {
        idSGameMain::Error("PrintMsg overrun");
    }

    va_end(argptr);

    // double quotes are bad
    while((p = strchr(msg, '"')) != nullptr) {
        *p = '\'';
    }

    trap_SendServerCommand(((ent == nullptr) ? -1 : ent - g_entities),
                           va(nullptr, "print \"%s\"", msg));
}

/*
================
idSGameTeam::TeamFromString

Return the team referenced by a string
================
*/
team_t idSGameTeam::TeamFromString(valueType *str) {
    switch(tolower(*str)) {
        case '0':
        case 's':
            return TEAM_NONE;

        case '1':
        case 'a':
            return TEAM_ALIENS;

        case '2':
        case 'h':
            return TEAM_HUMANS;

        default:
            return NUM_TEAMS;
    }
}

/*
==============
idSGameTeam::OnSameTeam
==============
*/
bool idSGameTeam::OnSameTeam(gentity_t *ent1, gentity_t *ent2) {
    if(!ent1->client || !ent2->client) {
        return false;
    }

    if(ent1->client->pers.teamSelection == ent2->client->pers.teamSelection) {
        return true;
    }

    return false;
}

/*
==================
idSGameTeam::LeaveTeam
==================
*/
void idSGameTeam::LeaveTeam(gentity_t *self) {
    team_t team = self->client->pers.teamSelection;
    gentity_t *ent;
    sint i;

    if(team == TEAM_ALIENS) {
        idSGameMain::RemoveFromSpawnQueue(&level.alienSpawnQueue,
                                          self->client->ps.clientNum);
    } else if(team == TEAM_HUMANS) {
        idSGameMain::RemoveFromSpawnQueue(&level.humanSpawnQueue,
                                          self->client->ps.clientNum);
    } else {
        if(self->client->sess.spectatorState == SPECTATOR_FOLLOW) {
            idSGameCmds::StopFollowing(self);
        }

        return;
    }

    // stop any following clients
    idSGameCmds::StopFromFollowing(self);

    idSGameMain::TeamVote(self, false);
    self->suicideTime = 0;

    for(i = 0; i < level.num_entities; i++) {
        ent = &g_entities[ i ];

        if(!ent->inuse) {
            continue;
        }

        if(ent->client && ent->client->pers.connected == CON_CONNECTED) {
            // cure poison
            if(ent->client->ps.stats[STAT_STATE] & SS_POISONED &&
                    ent->client->lastPoisonClient == self) {
                ent->client->ps.stats[STAT_STATE] &= ~SS_POISONED;
            }
        } else if(ent->s.eType == ET_MISSILE &&
                  ent->r.ownerNum == self->s.number) {
            idSGameUtils::FreeEntity(ent);
        }
    }
}

/*
=================
idSGameTeam::ChangeTeam
=================
*/
void idSGameTeam::ChangeTeam(gentity_t *ent, team_t newTeam) {
    team_t oldTeam = ent->client->pers.teamSelection;

    if(oldTeam == newTeam) {
        return;
    }

    LeaveTeam(ent);
    ent->client->pers.teamSelection = newTeam;

    // under certain circumstances, clients can keep their kills and credits
    // when switching teams
    if(adminLocal.AdminPermission(ent, ADMF_TEAMCHANGEFREE) ||
            ((oldTeam == TEAM_HUMANS || oldTeam == TEAM_ALIENS) &&
             (level.time - ent->client->pers.teamChangeTime) > 60000)) {
        if(oldTeam == TEAM_NONE) {
            // ps.persistant[] from a spectator cannot be trusted
            ent->client->ps.persistant[ PERS_CREDIT ] = ent->client->pers.savedCredit;
        } else if(oldTeam == TEAM_HUMANS) {
            // always save in alien credits
            ent->client->ps.persistant[ PERS_CREDIT ] *= (ALIEN_MAX_CREDITS /
                    (float32)HUMAN_MAX_CREDITS);
        }

        if(newTeam == TEAM_NONE) {
            // save values before the client enters the spectator team and their
            // ps.persistant[] values become trashed
            ent->client->pers.savedCredit = ent->client->ps.persistant[ PERS_CREDIT ];
        } else if(newTeam == TEAM_HUMANS) {
            // convert to alien currency
            ent->client->ps.persistant[ PERS_CREDIT ] *= (HUMAN_MAX_CREDITS /
                    (float32)ALIEN_MAX_CREDITS);
        }
    } else {
        ent->client->ps.persistant[ PERS_CREDIT ] = 0;
        ent->client->ps.persistant[ PERS_SCORE ] = 0;
        ent->client->pers.savedCredit = 0;
    }

    ent->client->pers.classSelection = PCL_NONE;
    idSGameClient::ClientSpawn(ent, nullptr, nullptr, nullptr);

    ent->client->pers.joinedATeam = true;
    ent->client->pers.teamChangeTime = level.time;

    //update ClientInfo
    sgameLocal.ClientUserinfoChanged(ent->client->ps.clientNum);

    if(oldTeam != TEAM_NONE && newTeam != TEAM_NONE) {
        idSGameMain::LogPrintf("team: %i %i %i: %s" S_COLOR_WHITE
                               " left the %ss and joined the %ss\n", ent->s.number, newTeam, oldTeam,
                               ent->client->pers.netname, bggame->TeamName(oldTeam),
                               bggame->TeamName(newTeam));
    } else if(newTeam == TEAM_NONE) {
        idSGameMain::LogPrintf("team: %i %i %i: %s" S_COLOR_WHITE
                               " left the %ss\n", ent->s.number, newTeam, oldTeam,
                               ent->client->pers.netname, bggame->TeamName(oldTeam));
    } else {
        idSGameMain::LogPrintf("team: %i %i %i: %s" S_COLOR_WHITE
                               " joined the %ss\n", ent->s.number, newTeam, oldTeam,
                               ent->client->pers.netname, bggame->TeamName(newTeam));
    }
}

/*
===========
idSGameTeam::Team_GetLocation

Report a location for the player. Uses placed nearby target_location entities
============
*/
gentity_t *idSGameTeam::Team_GetLocation(gentity_t *ent) {
    gentity_t *eloc, *best;
    float32 bestlen, len;
    vec3_t origin;

    best = nullptr;
    bestlen = 3.0f * 8192.0f * 8192.0f;

    VectorCopy(ent->r.currentOrigin, origin);

    for(eloc = level.locationHead; eloc; eloc = eloc->nextTrain) {
        len = (origin[ 0 ] - eloc->r.currentOrigin[ 0 ]) * (origin[ 0 ] -
                eloc->r.currentOrigin[ 0 ])
              + (origin[ 1 ] - eloc->r.currentOrigin[ 1 ]) * (origin[ 1 ] -
                      eloc->r.currentOrigin[ 1 ])
              + (origin[ 2 ] - eloc->r.currentOrigin[ 2 ]) * (origin[ 2 ] -
                      eloc->r.currentOrigin[ 2 ]);

        if(len > bestlen) {
            continue;
        }

        if(!trap_InPVS(origin, eloc->r.currentOrigin)) {
            continue;
        }

        bestlen = len;
        best = eloc;
    }

    return best;
}


/*
===========
idSGameTeam::Team_GetLocationMsg

Report a location message for the player. Uses placed nearby target_location entities
============
*/
bool idSGameTeam::Team_GetLocationMsg(gentity_t *ent, valueType *loc,
                                      sint loclen) {
    gentity_t *best;

    best = Team_GetLocation(ent);

    if(!best) {
        return false;
    }

    if(best->count) {
        if(best->count < 0) {
            best->count = 0;
        }

        if(best->count > 7) {
            best->count = 7;
        }

        Q_vsprintf_s(loc, loclen, loclen, "%c%c%s" S_COLOR_WHITE, Q_COLOR_ESCAPE,
                     best->count + '0', best->message);
    } else {
        Q_vsprintf_s(loc, loclen, loclen, "%s", best->message);
    }

    return true;
}

/*
==============
idSGameTeam::SortClients
==============
*/
sint idSGameTeam::SortClients(const void *a, const void *b) {
    return *(sint *)a - *(sint *)b;
}


/*
==================
idSGameTeam::TeamplayInfoMessage

clientNum location health armor weapon misc
==================
*/
void idSGameTeam::TeamplayInfoMessage(gentity_t *ent) {
    valueType entry[ 1024 ];
    valueType string[ 8192 ];
    sint stringlength, i, j;
    gentity_t *player;
    sint cnt, h, a = 0, clients[ TEAM_MAXOVERLAY ];

    if(!ent->client->pers.teamInfo) {
        return;
    }

    // figure out what client should be on the display
    // we are limited to 8, but we want to use the top eight players
    // but in client order (so they don't keep changing position on the overlay)
    for(i = 0, cnt = 0; i < g_maxclients.integer &&
            cnt < TEAM_MAXOVERLAY; i++) {
        player = g_entities + level.sortedClients[ i ];

        if(player->inuse &&
                player->client->ps.stats[STAT_TEAM] == ent->client->ps.stats[STAT_TEAM]) {
            clients[cnt++] = level.sortedClients[i];
        }
    }

    // We have the top eight players, sort them by clientNum
    qsort(clients, cnt, sizeof(clients[ 0 ]), SortClients);

    // send the latest information on all clients
    string[ 0 ] = 0;
    stringlength = 0;

    for(i = 0, cnt = 0; i < g_maxclients.integer &&
            cnt < TEAM_MAXOVERLAY; i++) {
        player = g_entities + i;

        if(player->inuse &&
                player->client->ps.stats[ STAT_TEAM ] ==
                ent->client->ps.stats[ STAT_TEAM ]) {
            h = player->client->ps.stats[ STAT_HEALTH ];

            if(h < 0) {
                h = 0;
            }

            Q_vsprintf_s(entry, sizeof(entry), sizeof(entry), " %i %i %i %i %i", i,
                         player->client->pers.location, h, a, player->client->ps.weapon);

            j = strlen(entry);

            if(stringlength + j > sizeof(string)) {
                break;
            }

            strcpy(string + stringlength, entry);
            stringlength += j;
            cnt++;
        }
    }

    trap_SendServerCommand(ent - g_entities, va(nullptr, "tinfo %i %s", cnt,
                           string));
}

/*
==============
idSGameTeam::CheckTeamStatus
==============
*/
void idSGameTeam::CheckTeamStatus(void) {
    sint i;
    gentity_t *loc, *ent;

    if(level.time - level.lastTeamLocationTime > TEAM_LOCATION_UPDATE_TIME) {
        level.lastTeamLocationTime = level.time;

        for(i = 0; i < g_maxclients.integer; i++) {
            ent = g_entities + i;

            if(ent->client->pers.connected != CON_CONNECTED) {
                continue;
            }

            if(ent->inuse && (ent->client->ps.stats[ STAT_TEAM ] == TEAM_HUMANS ||
                              ent->client->ps.stats[ STAT_TEAM ] == TEAM_ALIENS)) {
                loc = Team_GetLocation(ent);

                if(loc) {
                    ent->client->pers.location = loc->health;
                } else {
                    ent->client->pers.location = 0;
                }
            }
        }

        for(i = 0; i < g_maxclients.integer; i++) {
            ent = g_entities + i;

            if(ent->client->pers.connected != CON_CONNECTED) {
                continue;
            }

            if(ent->inuse && (ent->client->ps.stats[STAT_TEAM] == TEAM_HUMANS ||
                              ent->client->ps.stats[STAT_TEAM] == TEAM_ALIENS)) {
                TeamplayInfoMessage(ent);
            }
        }
    }
}
