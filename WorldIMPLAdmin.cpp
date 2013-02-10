//  illarionserver - server for the game Illarion
//  Copyright 2011 Illarion e.V.
//
//  This file is part of illarionserver.
//
//  illarionserver is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  illarionserver is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with illarionserver.  If not, see <http://www.gnu.org/licenses/>.


#include "World.hpp"
#include <sstream>
#include <regex.h>
#include <list>
#include "data/TilesModificatorTable.hpp"
#include "data/TilesTable.hpp"
#include "data/QuestTable.hpp"
#include "data/SkillTable.hpp"
#include "data/ArmorObjectTable.hpp"
#include "data/WeaponObjectTable.hpp"
#include "data/ContainerObjectTable.hpp"
#include "script/LuaLookAtPlayerScript.hpp"
#include "script/LuaLookAtItemScript.hpp"
#include "script/LuaPlayerDeathScript.hpp"
#include "script/LuaDepotScript.hpp"
#include "data/CommonObjectTable.hpp"
#include "data/NamesObjectTable.hpp"
#include "data/MonsterTable.hpp"
#include "data/SpellTable.hpp"
#include "data/TriggerTable.hpp"
#include "data/MonsterAttackTable.hpp"
#include "data/NaturalArmorTable.hpp"
#include "data/ScheduledScriptsTable.hpp"
//We need this for the standard Fighting Script.
#include "script/LuaWeaponScript.hpp"
//For the reload scripts
#include "script/LuaReloadScript.hpp"
#include "script/LuaLearnScript.hpp"
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include "data/LongTimeEffectTable.hpp"
#include "netinterface/protocol/ServerCommands.hpp"
#include "netinterface/NetInterface.hpp"
#include "script/LuaLoginScript.hpp"
#include "script/LuaLogoutScript.hpp"
#include "PlayerManager.hpp"
#include "data/RaceSizeTable.hpp"
#include "Logger.hpp"
#include "data/ScriptVariablesTable.hpp"
#include "data/QuestNodeTable.hpp"
#include "constants.hpp"

#include <iostream>

extern QuestTable *Quests;
extern LongTimeEffectTable *LongTimeEffects;
extern RaceSizeTable *RaceSizes;
extern ScriptVariablesTable *scriptVariables;
extern std::ofstream talkfile;
extern boost::shared_ptr<LuaLookAtPlayerScript>lookAtPlayerScript;
extern boost::shared_ptr<LuaLookAtItemScript>lookAtItemScript;
extern boost::shared_ptr<LuaPlayerDeathScript>playerDeathScript;
extern boost::shared_ptr<LuaDepotScript>depotScript;
extern boost::shared_ptr<LuaLoginScript>loginScript;
extern boost::shared_ptr<LuaLogoutScript>logoutScript;
extern boost::shared_ptr<LuaLearnScript>learnScript;


void set_spawn_command(World *, Player *, const std::string &);
void import_maps_command(World *, Player *, const std::string &);
void create_area_command(World *, Player *, const std::string &);
void set_login(World *, Player *, const std::string &);

template< typename To, typename From> To stream_convert(const From &from) {
    std::stringstream stream;
    stream << from;
    To to;
    stream >> to;
    return to;
}

// register any gm commands here...
void World::InitGMCommands() {

    GMCommands["what"] = [](World *world, Player *player, const std::string &) -> bool { world->what_command(player); return true; };

    GMCommands["?"] = [](World *world, Player *player, const std::string &) -> bool { world->gmhelp_command(player); return true; };

    GMCommands["warp_to"] = [](World *world, Player *player, const std::string &text) -> bool { world->warpto_command(player, text); return true; };
    GMCommands["w"] = GMCommands["warp_to"];

    GMCommands["summon"] = [](World *world, Player *player, const std::string &text) -> bool { world->summon_command(player, text); return true; };
    GMCommands["s"] = GMCommands["summon"];

    GMCommands["ban"] = [](World *world, Player *player, const std::string &text) -> bool { world->ban_command(player, text); return true; };
    GMCommands["b"] = GMCommands["ban"];

    GMCommands["tile"] = [](World *world, Player *player, const std::string &text) -> bool { world->tile_command(player, text); return true; };
    GMCommands["t"] = GMCommands["tile"];

    GMCommands["who"] = [](World *world, Player *player, const std::string &text) -> bool { world->who_command(player, text); return true; };

    GMCommands["turtleon"] = [](World *world, Player *player, const std::string &text) -> bool { world->turtleon_command(player, text); return true; };
    GMCommands["ton"] = GMCommands["turtleon"];
    GMCommands["turtleoff"] = [](World *world, Player *player, const std::string &) -> bool { world->turtleoff_command(player); return true; };
    GMCommands["toff"] = GMCommands["turtleoff"];

    GMCommands["logon"] = [](World *world, Player *player, const std::string &text) -> bool { world->logon_command(player, text); return true; };
    GMCommands["logoff"] = [](World *world, Player *player, const std::string &text) -> bool { world->logoff_command(player, text); return true; };

    GMCommands["clippingon"] = [](World *world, Player *player, const std::string &) -> bool { world->clippingon_command(player); return true; };
    GMCommands["con"] = GMCommands["clippingon"];
    GMCommands["clippingoff"] = [](World *world, Player *player, const std::string &) -> bool { world->clippingoff_command(player); return true; };
    GMCommands["coff"] = GMCommands["clippingoff"];

    GMCommands["playersave"] = [](World *world, Player *player, const std::string &) -> bool { world->playersave_command(player); return true; };
    GMCommands["ps"] = GMCommands["playersave"];

    GMCommands["add_teleport"] = [](World *world, Player *player, const std::string &text) -> bool { world->teleport_command(player, text); return true; };

    GMCommands["set_spawn"] = [](World *world, Player *player, const std::string &text) -> bool { set_spawn_command(world, player, text); return true; };

    GMCommands["importmaps"] = [](World *world, Player *player, const std::string &text) -> bool { import_maps_command(world, player, text); return true; };

    GMCommands["create_area"] = [](World *world, Player *player, const std::string &text) -> bool { create_area_command(world, player, text); return true; };

    GMCommands["nologin"] = [](World *world, Player *player, const std::string &text) -> bool { set_login(world, player, text); return true; };

    GMCommands["forceintroduce"] = [](World *world, Player *player, const std::string &text) -> bool { world->ForceIntroduce(player, text); return true; };
    GMCommands["fi"] = GMCommands["forceintroduce"];
    GMCommands["forceintroduceall"] = [](World *world, Player *player, const std::string &) -> bool { world->ForceIntroduceAll(player); return true; };
    GMCommands["fia"] = GMCommands["forceintroduceall"];

    GMCommands["exportmaps"] = [](World *world, Player *player, const std::string &) -> bool { return world->exportMaps(player); };

    GMCommands["makeinvisible"] = [](World *world, Player *player, const std::string &) -> bool { world->makeInvisible(player); return true; };
    GMCommands["mi"] = GMCommands["makeinvisible"];

    GMCommands["makevisible"] = [](World *world, Player *player, const std::string &) -> bool { world->makeVisible(player); return true; };
    GMCommands["mv"] = GMCommands["makevisible"];

    GMCommands["showwarpfields"] = [](World *world, Player *player, const std::string &text) -> bool { world->showWarpFieldsInRange(player, text); return true; };

    GMCommands["removewarpfield"] = [](World *world, Player *player, const std::string &text) -> bool { world->removeTeleporter(player, text); return true; };

    GMCommands["inform"] = [](World *world, Player *player, const std::string &) -> bool { world->informChar(player); return true; };

    GMCommands["talkto"] = [](World *world, Player *player, const std::string &text) -> bool { world->talkto_command(player, text); return true; };
    GMCommands["tt"] = GMCommands["talkto"];

    GMCommands["nuke"] = [](World *world, Player *player, const std::string &) -> bool { world->kill_command(player); return true; };

    GMCommands["fullreload"] = [](World *world, Player *player, const std::string &) -> bool { world->reload_command(player); return true; };
    GMCommands["fr"] = GMCommands["fullreload"];

    GMCommands["mapsave"] = [](World *world, Player *player, const std::string &) -> bool { world->save_command(player); return true; };

    GMCommands["jumpto"] = [](World *world, Player *player, const std::string &text) -> bool { world->jumpto_command(player, text); return true; };
    GMCommands["j"] = GMCommands["jumpto"];

    GMCommands["broadcast"] = [](World *world, Player *player, const std::string &text) -> bool { world->broadcast_command(player, text); return true; };
    GMCommands["bc"] = GMCommands["broadcast"];

    GMCommands["kickall"] = [](World *world, Player *player, const std::string &) -> bool { world->kickall_command(player); return true; };
    GMCommands["ka"] = GMCommands["kickall"];

    GMCommands["kick"] = [](World *world, Player *player, const std::string &text) -> bool { world->kickplayer_command(player, text); return true; };
    GMCommands["k"] = GMCommands["kick"];

    GMCommands["showips"] = [](World *world, Player *player, const std::string &) -> bool { world->showIPS_Command(player); return true; };
    GMCommands["create"] = [](World *world, Player *player, const std::string &text) -> bool { world->create_command(player, text); return true; };

    GMCommands["spawn"] = [](World *world, Player *player, const std::string &text) -> bool { world->spawn_command(player, text); return true; };

}

void World::logon_command(Player *cp, const std::string &log) {
    if (cp->hasGMRight(gmr_reload)) {
        Logger::writeMessage("admin", cp->nameAndId() + " activates log: " + log);
        Logger::activateLog(log);
    }

}

void World::logoff_command(Player *cp, const std::string &log) {
    if (cp->hasGMRight(gmr_reload)) {
        Logger::writeMessage("admin", cp->nameAndId() + " deactivates log: " + log);
        Logger::deactivateLog(log);
    }

}

void World::spawn_command(Player *cp, const std::string &monid) {
    if (cp->hasGMRight(gmr_basiccommands)) {
        uint16_t id;
        std::stringstream ss;
        ss.str(monid);
        ss >> id;
        position pos = cp->pos;
        pos.x++;
        Logger::writeMessage("admin", cp->nameAndId() + " creates monster " + monid
                             + " at " + pos.toString());
        createMonster(id, pos, 0);
    }
}

void World::create_command(Player *cp, const std::string &itemid) {
#ifndef TESTSERVER

    if (cp->hasGMRight(gmr_basiccommands))
#endif
    {
        TYPE_OF_ITEM_ID item;
        uint16_t quantity = 1;
        uint16_t quality = 333;
        std::string data;
        std::string datalog;
        lua_State *luaState = LuaScript::getLuaState();
        luabind::object dataList = luabind::newtable(luaState);

        std::stringstream ss;
        ss.str(itemid);
        ss >> item;
        ss >> quantity;
        ss >> quality;

        while (ss.good()) {
            ss >> data;
            size_t found = data.find('=');

            if (found != string::npos) {
                std::string key = data.substr(0, int(found));
                std::string value = data.substr(int(found) + 1);
                datalog += key + "=" + value + "; ";
                dataList[key] = value;
            }
        }

        Logger::writeMessage("admin", cp->nameAndId() + " creates item " + boost::lexical_cast<std::string>(item) + " with quantity "
                             + boost::lexical_cast<std::string>(quantity) + ", quality "
                             + boost::lexical_cast<std::string>(quality) + ", data " + datalog);
        cp->createItem(item, quantity, quality, dataList);
    }

}

void World::kill_command(Player *cp) {
    if (!cp->hasGMRight(gmr_reload)) {
        return;
    }

    MONSTERVECTOR::iterator mIterator;
    uint32_t counter = 0;

    for (mIterator = Monsters.begin() ; mIterator != Monsters.end(); ++mIterator) {
        //Kill the monster we have found
        (*mIterator)->remove();
        ++counter;
    }

    Logger::writeMessage("admin", cp->nameAndId() + " nukes " + boost::lexical_cast<std::string>(counter) + " monsters");
}

void World::reload_command(Player *cp) {
    if (cp->hasGMRight(gmr_reload)) {
        Logger::writeMessage("admin", cp->nameAndId() + " issues a full reload");

        if (reload_tables(cp)) {
            cp->inform("DB tables loaded successfully!");
        } else {
            cp->inform("CRITICAL ERROR: Failure while loading DB tables!");
        }
    }
}

void World::broadcast_command(Player *cp,const std::string &message) {
    if (cp->hasGMRight(gmr_broadcast)) {
#ifdef LOG_TALK
        time_t acttime = time(NULL);
        std::string talktime = ctime(&acttime);
        talktime[talktime.size()-1] = ':';
        talkfile << talktime << " ";
        talkfile << cp->name << "(" << cp->id << ") broadcasts: " << message << std::endl;
#endif
        sendMessageToAllPlayers(message);
    }
}

void World::kickall_command(Player *cp) {
    if (cp->hasGMRight(gmr_forcelogout)) {
        Logger::writeMessage("admin", cp->nameAndId() + " kicks all players");
        forceLogoutOfAllPlayers();
    }
}

void World::kickplayer_command(Player *cp,const std::string &player) {
    if (cp->hasGMRight(gmr_forcelogout)) {
        Logger::writeMessage("admin", cp->nameAndId() + " kicks " + player);
        forceLogoutOfPlayer(player);
    }
}

void World::showIPS_Command(Player *cp) {
    if (cp->hasGMRight(gmr_basiccommands)) {
        Logger::writeMessage("admin", cp->nameAndId() + " requests player info");
        sendAdminAllPlayerData(cp);
    }
}

void World::jumpto_command(Player *cp,const std::string &player) {
#ifndef TESTSERVER

    if (cp->hasGMRight(gmr_warp))
#endif
    {
        cp->closeAllShowcasesOfMapContainers();
        teleportPlayerToOther(cp, player);
        Logger::writeMessage("admin", cp->nameAndId() + " jumps to player " + player
                             + " at " + cp->pos.toString());
    }
}

void World::save_command(Player *cp) {
    if (!cp->hasGMRight(gmr_save)) {
        return;
    }

    Logger::writeMessage("admin", cp->nameAndId() + " saves all maps");

    PLAYERVECTOR::iterator pIterator;
    Field *tempf;

    for (pIterator = Players.begin(); pIterator < Players.end(); ++pIterator) {
        // Felder auf denen Spieler stehen als frei markieren, damit die flags richtig gespeichert werden
        if (GetPToCFieldAt(tempf, (*pIterator)->pos.x, (*pIterator)->pos.y, (*pIterator)->pos.z)) {
            tempf->SetPlayerOnField(false);
        }
    }

    std::cout << "Save maps" << std::endl;
    Save("Illarion");

    // Flags auf der Karte wieder setzen
    for (pIterator = Players.begin(); pIterator < Players.end(); ++pIterator) {
        if (GetPToCFieldAt(tempf, (*pIterator)->pos.x, (*pIterator)->pos.y, (*pIterator)->pos.z)) {
            tempf->SetPlayerOnField(true);
        }
    }

    std::string tmessage = "*** Maps saved! ***";
    cp->inform(tmessage);
}

void World::talkto_command(Player *cp, const std::string &ts) {
    if (!cp->hasGMRight(gmr_basiccommands)) {
        return;    //quit if the player hasn't the right
    }

    char *tokenize = new char[ ts.length() + 1 ]; //Neuen char mit gr�e des strings anlegen
    strcpy(tokenize, ts.c_str());   //Copy ts to tokenize
    std::cout<<"Tokenizing "<<tokenize<<std::endl;
    char *token;

    if ((token = strtok(tokenize, ",")) != NULL) {
        std::string player = token;
        delete[] tokenize;

        if ((token = strtok(NULL, "\\")) != NULL) {
            std::string message = token;
#ifdef AdminCommands_DEBUG
            std::cout<<"Try to find player by name: "<<player<<std::endl;
#endif
            Player *tempPl = Players.find(player);

            if (tempPl != NULL) {
#ifdef AdminCommands_DEBUG
                std::cout<<"Found player by name, sending message: "<<message<<std::endl;
#endif
#ifdef LOG_TALK
                time_t acttime = time(NULL);
                std::string talktime = ctime(&acttime);
                talktime[talktime.size()-1] = ':';
                talkfile << talktime << " ";
                talkfile << cp->name << "(" << cp->id << ") talks to player " << tempPl->name << "(" << tempPl->id << "): " << message << std::endl;
#endif
                tempPl->inform(message, Player::informGM);
                return;
            } else {
                TYPE_OF_CHARACTER_ID tid;

                std::stringstream ss;
                ss.str(player);
                ss >> tid;

#ifdef AdminCommands_DEBUG
                std::cout<<"Try to find player by id: "<<tid<<std::endl;
#endif
                PLAYERVECTOR::iterator plIterator;

                for (plIterator = Players.begin(); plIterator != Players.end(); ++plIterator) {
                    if ((*plIterator)->id == tid) {
#ifdef AdminCommands_DEBUG
                        std::cout<<"Found player by id, sending message: "<<message<<std::endl;
#endif
#ifdef LOG_TALK
                        time_t acttime = time(NULL);
                        std::string talktime = ctime(&acttime);
                        talktime[talktime.size()-1] = ':';
                        talkfile << talktime << " ";
                        talkfile << cp->name << "(" << cp->id << ") talks to player "
                                 << (*plIterator)->name << "(" << (*plIterator)->id << "): " << message << std::endl;
#endif
                        (*plIterator)->inform(message, Player::informGM);
                        return; //Break the loop because we sen our message
                    }
                } //end of for ( plIter..)
            } //end of else
        } //end if ( token = strtok( NULL...)
    } //end of ( token = strtok( tokenize , ",")
    else {
        delete[] tokenize;
    }
}

void World::makeInvisible(Player *cp) {
    if (!cp->hasGMRight(gmr_visible)) {
        return;
    }

    cp->isinvisible = true;
    Logger::writeMessage("admin", cp->nameAndId() + " becomes invisible");
    sendRemoveCharToVisiblePlayers(cp->id, cp->pos);
}

void World::makeVisible(Player *cp) {
    if (!cp->hasGMRight(gmr_visible)) {
        return;
    }

    cp->isinvisible = false;

    Logger::writeMessage("admin", cp->nameAndId() + " becomes visible");

    std::vector < Player * > ::iterator titerator;

    std::vector < Player * > temp = Players.findAllCharactersInScreen(cp->pos.x, cp->pos.y, cp->pos.z);

    for (titerator = temp.begin(); titerator < temp.end(); ++titerator) {
        //Fr alle anderen Sichtbar machen
        if (cp != (*titerator)) {
            boost::shared_ptr<BasicServerCommand>cmd(new MoveAckTC(cp->id, cp->pos, PUSH, 0));
            (*titerator)->Connection->addCommand(cmd);
        }
    }

    boost::shared_ptr<BasicServerCommand>cmd(new AppearanceTC(cp, cp));
    cp->Connection->addCommand(cmd);
}

void World::ForceIntroduce(Player *cp, const std::string &ts) {
    if (!cp->hasGMRight(gmr_basiccommands)) {
        return;
    }

    Player *tempPl;
    tempPl = Players.find(ts);

    if (tempPl != NULL) {
        forceIntroducePlayer(tempPl, cp);
    } else {
        TYPE_OF_CHARACTER_ID tid;

        // convert arg to digit and try again...
        std::stringstream ss;
        ss.str(ts);
        ss >> tid;

        if (tid) {
            PLAYERVECTOR::iterator playerIterator;

            for (playerIterator = Players.begin(); playerIterator < Players.end(); ++playerIterator) {
                if ((*playerIterator)->id == tid) {
                    forceIntroducePlayer((*playerIterator), cp);
                }
            }
        }
    }
}

void World::ForceIntroduceAll(Player *cp) {
    if (!cp->hasGMRight(gmr_basiccommands)) {
        return;
    }

    std::vector < Player * > temp = Players.findAllCharactersInRangeOf(cp->pos.x, cp->pos.y, cp->pos.z, cp->getScreenRange());
    std::vector < Player * > ::iterator titerator;

    for (titerator = temp.begin(); titerator < temp.end(); ++titerator) {
        //Schleife durch alle Spieler in Sichtweise
        if (cp != (*titerator)) {
            forceIntroducePlayer((*titerator), cp);
        }
    }
}

void World::informChar(Player *cp) {
    if (!cp->hasGMRight(gmr_basiccommands)) {
        return;
    }

    if (cp->informCharacter) {
        cp->setInformChar(false);
    } else {
        cp->setInformChar(true);
    }
}

void World::teleportPlayerToOther(Player *cp, std::string ts) {
    if (!cp->hasGMRight(gmr_warp)) {
        return;
    }

    Player *tempPl;
    tempPl = Players.find(ts);

    if (tempPl != NULL) {
        cp->Warp(tempPl->pos);
        //warpPlayer( cp, tempPl->pos );
    } else {
        TYPE_OF_CHARACTER_ID tid;

        // convert arg to digit and try again...
        std::stringstream ss;
        ss.str(ts);
        ss >> tid;

        if (tid) {
            PLAYERVECTOR::iterator playerIterator;

            for (playerIterator = Players.begin(); playerIterator < Players.end(); ++playerIterator) {
                if ((*playerIterator)->id == tid) {
                    //warpPlayer( cp, ( *playerIterator )->pos );
                    cp->Warp((*playerIterator)->pos);
                }
            }
        }
    }
}


void World::forceLogoutOfAllPlayers() {
    Field *tempf;
    PLAYERVECTOR::iterator playerIterator;

    for (playerIterator = Players.begin(); playerIterator < Players.end(); ++playerIterator) {
        if (GetPToCFieldAt(tempf, (*playerIterator)->pos.x, (*playerIterator)->pos.y, (*playerIterator)->pos.z)) {
            tempf->SetPlayerOnField(false);
        }

        Logger::writeMessage("admin", "--- kicked: " + (*playerIterator)->nameAndId());
        boost::shared_ptr<BasicServerCommand>cmd(new LogOutTC(SERVERSHUTDOWN));
        (*playerIterator)->Connection->shutdownSend(cmd);
        PlayerManager::get()->getLogOutPlayers().non_block_push_back(*playerIterator);
    }

    Players.clear();
}


bool World::forceLogoutOfPlayer(std::string name) {
    Player *temp = Players.find(name);

    if (temp != NULL) {
        Logger::writeMessage("admin", "--- kicked: " + temp->nameAndId());
        boost::shared_ptr<BasicServerCommand>cmd(new LogOutTC(BYGAMEMASTER));
        temp->Connection->shutdownSend(cmd);
        return true;
    } else {
        return false;
    }
}


void World::sendAdminAllPlayerData(Player* &admin) {
    if (!admin->hasGMRight(gmr_basiccommands)) {
        return;
    }

    boost::shared_ptr<BasicServerCommand>cmd(new AdminViewPlayersTC());
    admin->Connection->addCommand(cmd);

}


// !warp_to X<,| >Y[<,| >Z] || !warp_to Z
void World::warpto_command(Player *cp, const std::string &ts) {
#ifndef TESTSERVER

    if (!cp->hasGMRight(gmr_warp)) {
        return;
    }

#endif

    position warpto;
    char *tokenize = new char[ ts.length() + 1 ];

    strcpy(tokenize, ts.c_str());
    std::cout << "Tokenizing " << tokenize << std::endl;
    char *thistoken;

    if ((thistoken = strtok(tokenize, " ,")) != NULL) {
        if (ReadField(thistoken, warpto.x)) {
            if ((thistoken = strtok(NULL, " ,")) != NULL) {
                if (ReadField(thistoken, warpto.y)) {
                    if ((thistoken = strtok(NULL, " ,")) != NULL) {
                        if (ReadField(thistoken, warpto.z)) {
                            //warpPlayer( cp, warpto );
                            cp->forceWarp(warpto);
                        }
                    }
                    // Must give X and Y, but not Z
                    else {
                        warpto.z = cp->pos.z;
                        cp->forceWarp(warpto);
                        //warpPlayer( cp, warpto );
                    }
                }
            }
            // Enable !warp_to Z for easy level change
            else {
                warpto.z = warpto.x;
                warpto.x = cp->pos.x;
                warpto.y = cp->pos.y;
                cp->forceWarp(warpto);
                //warpPlayer( cp, warpto );
            }
        }
    }

    Logger::writeMessage("admin", cp->nameAndId() + " warps to " + warpto.toString());

    delete [] tokenize;
}


// !summon <player>
void World::summon_command(Player *cp, const std::string &tplayer) {
    if (!cp->hasGMRight(gmr_summon)) {
        return;
    }

    Player *tempPl;
    tempPl = Players.find(tplayer);

    if (tempPl != NULL) {
        Logger::writeMessage("admin", cp->nameAndId() + " summons player " + tempPl->nameAndId() + " to " + cp->pos.toString());
        tempPl->Warp(cp->pos);
    } else {
        std::cout << "Looking for number" << std::endl;
        TYPE_OF_CHARACTER_ID tid;

        // convert arg to digit and try again...
        std::stringstream ss;
        ss.str(tplayer);
        ss >> tid;

        if (tid) {
            std::cout << "Looking for number " << tid << std::endl;
            PLAYERVECTOR::iterator playerIterator;

            for (playerIterator = Players.begin(); playerIterator < Players.end(); ++playerIterator) {
                if ((*playerIterator)->id == tid) {
                    Logger::writeMessage("admin", cp->nameAndId() + " summons player " + (*playerIterator)->nameAndId() + " to " + cp->pos.toString());
                    (*playerIterator)->Warp(cp->pos);
                }
            }
        }
    }

}


// !ban <time> [m|h|d] <player>
void World::ban_command(Player *cp, const std::string &timeplayer) {
    if (!cp->hasGMRight(gmr_ban)) {
        return;
    }

    char *tokenize = new char[ timeplayer.length() + 1 ];

    strcpy(tokenize, timeplayer.c_str());

    char *thistoken;

    if ((thistoken = strtok(tokenize, " ")) != NULL) {
        short int jailtime = 0;

        if (ReadField(thistoken, jailtime)) {
            char *tcharp = strtok(NULL, " ");

            if (tcharp != NULL) {
                int multiplier = 0;

                std::string tplayer;
                std::string timescale = tcharp;

                if (timescale == "m") {
                    multiplier = 60;
                    timescale = "";
                } else if (timescale == "h") {
                    multiplier = 3600;
                    timescale = "";
                } else if (timescale == "d") {
                    multiplier = 86400;
                    timescale = "";
                }

                char *tcharp = strtok(NULL, "\\");

                if (tcharp != NULL) {
                    tplayer = tcharp;

                    if (timescale != "") {
                        tplayer = timescale + " " + tplayer;
                    }
                } else {
                    tplayer = timescale;
                    timescale = "d";
                    multiplier = 86400;
                }

                Player *tempPl;
                tempPl = Players.find(tplayer);

                if (tempPl == NULL) {
                    TYPE_OF_CHARACTER_ID tid;

                    // convert arg to digit and try again...
                    std::stringstream ss;
                    ss.str(tplayer);
                    ss >> tid;

                    if (tid) {
                        PLAYERVECTOR::iterator playerIterator;

                        for (playerIterator = Players.begin(); playerIterator < Players.end(); ++playerIterator) {
                            if ((*playerIterator)->id == tid) {
                                tempPl = (*playerIterator);
                            }
                        }
                    }
                }

                if (tempPl != NULL) {

                    ban(tempPl, jailtime * multiplier, cp->id);

                    Logger::writeMessage("admin", cp->nameAndId() + " bans player " + tempPl->nameAndId() + " for " + boost::lexical_cast<std::string>(jailtime) + timescale);
                    std::string tmessage = "*** Banned " + tempPl->name;
                    cp->inform(tmessage);

                } else {
                    std::string tmessage = "*** Could not find " + tplayer;
                    std::cout << tmessage << std::endl;
                    cp->inform(tmessage);
                }
            }
        }
    }

    delete [] tokenize;

}

void World::banbyname(Player *cp, short int banhours, std::string tplayer) {
    if (!cp->hasGMRight(gmr_ban)) {
        return;
    }

    Player *tempPl;
    tempPl = Players.find(tplayer);

    if (tempPl != NULL) {

        ban(tempPl, static_cast<int>(banhours * 3600), cp->id);

        Logger::writeMessage("admin", cp->nameAndId() + " bans player " + tempPl->nameAndId() + " for " + boost::lexical_cast<std::string>(banhours) + "h");
        std::string tmessage = "*** Banned " + tempPl->name;
        cp->inform(tmessage);

    } else {
        std::string tmessage = "*** Could not find " + tplayer;
        std::cout << tmessage << std::endl;
        cp->inform(tmessage);
    }

}

void World::banbynumber(Player *cp, short int banhours, TYPE_OF_CHARACTER_ID tid) {
    if (!cp->hasGMRight(gmr_ban)) {
        return;
    }

    Player *tempPl = NULL;

    PLAYERVECTOR::iterator playerIterator;

    for (playerIterator = Players.begin(); playerIterator < Players.end(); ++playerIterator) {
        if ((*playerIterator)->id == tid) {
            tempPl = (*playerIterator);
        }
    }

    if (tempPl != NULL) {

        ban(tempPl, static_cast<int>(banhours * 3600), cp->id);

        Logger::writeMessage("admin", cp->nameAndId() + " bans player " + tempPl->nameAndId() + " for " + boost::lexical_cast<std::string>(banhours) + "h");
        std::string tmessage = "*** Banned " + tempPl->name;
        cp->inform(tmessage);

    } else {
        std::string tmessage = "*** Could not find " + stream_convert<std::string>(tid);
        std::cout << tmessage << std::endl;
        cp->inform(tmessage);
    }


}


void World::ban(Player *cp, int bantime, TYPE_OF_CHARACTER_ID gmid) {
    if (bantime >= 0) {
        if (bantime > 0) {
            cp->SetStatus(BANNEDFORTIME);         // Banned for time
            time_t ttime;
            time(&ttime);
            // Banned for seconds
            cp->SetStatusTime(ttime + bantime);    // Banned for expire
            cp->SetStatusGM(gmid);               // Banned by who
        } else if (bantime == 0) {
            cp->SetStatus(BANNED);                 // Banned indefinately
            cp->SetStatusTime(0);
            cp->SetStatusGM(gmid);               // Banned by who
        }

        forceLogoutOfPlayer(cp->name);

    }

}


// !who [player]
void World::who_command(Player *cp, const std::string &tplayer) {
    if (!cp->hasGMRight(gmr_basiccommands)) {
        return;
    }

    if (tplayer == "") {

        std::string tmessage = "";
        PLAYERVECTOR::iterator playerIterator;

        for (playerIterator = Players.begin(); playerIterator < Players.end(); ++playerIterator) {
            if (tmessage.length() > 0 && tmessage.length() + 10 + (*playerIterator)->name.length() > 104) {
                cp->inform(tmessage);
                tmessage = "";
            }

            if (tmessage.length() > 0) {
                tmessage = tmessage + ", ";
            }

            tmessage = tmessage + (*playerIterator)->name +
                       "(" + stream_convert<std::string>((*playerIterator)->id) + ")";
        }

        if (tmessage.length() > 0) {
            cp->inform(tmessage);
        }
    } else {

        Player *tempPl;
        tempPl = Players.find(tplayer);

        if (tempPl == NULL) {
            TYPE_OF_CHARACTER_ID tid;

            // convert arg to digit and try again...
            std::stringstream ss;
            ss.str(tplayer);
            ss >> tid;

            if (tid) {
                PLAYERVECTOR::iterator playerIterator;

                for (playerIterator = Players.begin(); playerIterator < Players.end(); ++playerIterator) {
                    if ((*playerIterator)->id == tid) {
                        tempPl = (*playerIterator);
                    }
                }
            }
        }

        if (tempPl != NULL) {
            std::string tmessage = tempPl->name +
                                   "(" + stream_convert<std::string>(tempPl->id) + ")";

            tmessage = tmessage + " x" + stream_convert<std::string>(tempPl->pos.x);
            tmessage = tmessage + " y" + stream_convert<std::string>(tempPl->pos.y);
            tmessage = tmessage + " z" + stream_convert<std::string>(tempPl->pos.z);
            tmessage = tmessage + " HPs:" + stream_convert<std::string>(tempPl->getAttribute(Character::hitpoints));
            tmessage = tmessage + ((tempPl->IsAlive()) ? " Alive" : " Dead");
            tmessage = tmessage + " Mental Capacity: " + stream_convert<std::string>(tempPl->getMentalCapacity());
#ifdef DO_UNCONSCIOUS

            if (! tempPl->IsConscious()) {
                tmessage = tmessage + " (Unconscious)";
            }

#endif
            std::string german = " German";
            std::string english = " English";
            tmessage = tmessage + tempPl->nls(german, english);

            cp->inform(tmessage);
        }
    }
}


void World::tile_command(Player *cp, const std::string &ttilenumber) {
    if (!cp->hasGMRight(gmr_settiles)) {
        return;
    }

    short int tilenumber = 0;

    if (ReadField(ttilenumber.c_str(), tilenumber)) {
        setNextTile(cp, tilenumber);
    }

}


void World::setNextTile(Player *cp, unsigned char tilenumber) {

    position tpos = cp->getFrontalPosition();

    Field *tempf;

    if (GetPToCFieldAt(tempf, tpos.x, tpos.y, tpos.z)) {
        tempf->setTileId(tilenumber);
        tempf->updateFlags();
    }

    //update the current area
    cp->sendRelativeArea(0);
    sendAllVisibleCharactersToPlayer(cp, true);

}


void World::turtleon_command(Player *cp, const std::string &ttilenumber) {
    if (!cp->hasGMRight(gmr_settiles)) {
        return;
    }

    short int tilenumber = 0;

    if (ReadField(ttilenumber.c_str(), tilenumber)) {
        cp->setTurtleActive(true);
        cp->setTurtleTile(tilenumber);
    }
}


void World::turtleoff_command(Player *cp) {
    if (!cp->hasGMRight(gmr_settiles)) {
        return;
    }

    cp->setTurtleActive(false);
}


void World::clippingon_command(Player *cp) {
    if (!cp->hasGMRight(gmr_clipping)) {
        return;
    }

    Logger::writeMessage("admin", cp->nameAndId() + " turns on clipping");
    cp->setClippingActive(true);
}


void World::clippingoff_command(Player *cp) {
    if (!cp->hasGMRight(gmr_clipping)) {
        return;
    }

    Logger::writeMessage("admin", cp->nameAndId() + " turns off clipping");
    cp->setClippingActive(false);
}


void World::what_command(Player *cp) {
    position front = cp->getFrontalPosition();

    cp->inform("Facing:");
    std::stringstream message;

    message << "- Position (" << front.x << ", " << front.y << ", " << front.z << ")";
    cp->inform(message.str());
    Field *tempf;

    if (GetPToCFieldAt(tempf, front)) {
        message.str("");

        message << "- Tile " << tempf->getTileId();
        cp->inform(message.str());
        Item top;

        if (tempf->ViewTopItem(top)) {
            message.str("");

            message << "- Item " << top.getId();

#ifndef TESTSERVER

            if (cp->hasGMRight(gmr_basiccommands))
#endif
            {
                message << ", Quality " << top.getQuality();

                if (top.getDataBegin() != top.getDataEnd()) {
                    message << ", Data";

                    for (auto it = top.getDataBegin(); it != top.getDataEnd(); ++it) {
                        message << " '" << it->first << "'='" << it->second << "'";
                    }
                }

                message << ", Wear " << (uint16_t)top.getWear();
            }

            cp->inform(message.str());
        }

        Character *character = findCharacterOnField(front.x, front.y, front.z);

        if (character != 0) {
            message.str("");
            uint32_t id = character->id;

            if (id >= DYNNPC_BASE) {
                message << "- Dynamic NPC";
            } else if (id >= NPC_BASE) {
                message << "- NPC " << id-NPC_BASE;
            } else if (id >= MONSTER_BASE) {
                message << "- Monster " << dynamic_cast<Monster *>(character)->getType();
            } else {
                message << "- Player";
            }

            cp->inform(message.str());
        }
    }
}


void World::playersave_command(Player *cp) {
    if (!cp->hasGMRight(gmr_save)) {
        return;
    }

    Logger::writeMessage("admin", cp->nameAndId() + " saves all players");

    PLAYERVECTOR::iterator pIterator;

    for (pIterator = Players.begin(); pIterator < Players.end(); ++pIterator) {
        (*pIterator)->save();
    }

    std::string tmessage = "*** All online players saved! ***";
    cp->inform(tmessage);

}


// !teleport X<,| >Y[<,| >Z]
void World::teleport_command(Player *cp, const std::string &ts) {

    if (!cp->hasGMRight(gmr_warp)) {
        return;
    }

    position teleportto;
    char *tokenize = new char[ ts.length() + 1 ];

    strcpy(tokenize, ts.c_str());
    std::cout << "Tokenizing " << tokenize << std::endl;
    char *thistoken;

    if ((thistoken = strtok(tokenize, " ,")) != NULL) {
        if (ReadField(thistoken, teleportto.x)) {
            if ((thistoken = strtok(NULL, " ,")) != NULL) {
                if (ReadField(thistoken, teleportto.y)) {
                    if ((thistoken = strtok(NULL, " ,")) != NULL) {
                        if (ReadField(thistoken, teleportto.z)) {
                            if (addWarpField(cp->pos, teleportto, 0, 0)) {
                                std::string tmessage = "*** Warp Field Added! ***";
                                cp->inform(tmessage);
                            } else {
                                std::string tmessage = "*** Warp Field *NOT* Added! ***";
                                cp->inform(tmessage);
                            };
                        }
                    }
                }
            }
        }
    }

    delete [] tokenize;
}


void World::gmhelp_command(Player *cp) {
    if (!cp->hasGMRight(gmr_basiccommands)) {
        return;
    }

    std::string tmessage = " <> - parameter.  [] - optional.  | = choice.  () = shortcut";
    cp->inform(tmessage);

    if (cp->hasGMRight(gmr_basiccommands)) {
        tmessage = "!what - sends different informations of the field or the character in front of you.";
        cp->inform(tmessage);
        tmessage = "!who [player] - List all players online or a single player if specified.";
        cp->inform(tmessage);
        tmessage = "!forceintroduce <char id|char name> - (!fi) introduces the char to all gms in range.";
        cp->inform(tmessage);
        tmessage = "!forceintroduceall - (!fia) introduces all chars in sight to you.";
        cp->inform(tmessage);
        tmessage = "!inform - gives you some more informations while fighting.";
        cp->inform(tmessage);
        tmessage = "!talkto <playername|id>, <message> - (!tt) sends a message to a specific player important is the , after the id or name!";
        cp->inform(tmessage);
        tmessage = "!broadcast <message> - (!bc) Broadcasts the message <message> to all players IG.";
        cp->inform(tmessage);
        tmessage = "!create id [quantity [quality [[data_key=data_value] ...]]] creates an item in your inventory.";

    }

    if (cp->hasGMRight(gmr_warp)) {
        tmessage = "!warp <x> <y> [z] - (!w) to x, y, z location.";
        cp->inform(tmessage);
        tmessage = "!add_teleport <x> <y> <z> - Adds a teleportfield in front of you to the field <x> <y> <z>.";
        cp->inform(tmessage);
        tmessage = "!showwarpfields <range> - Shows all warpfields in the range <range>.";
        cp->inform(tmessage);
        tmessage = "!removewarpfield <x> <y> <z> - Removes the warpfield at the position <x> <y> <z>.";
        cp->inform(tmessage);
        tmessage = "!jumpto <playerid|name> - (!j) teleports you to the player.";
        cp->inform(tmessage);

    }

    if (cp->hasGMRight(gmr_summon)) {
        tmessage = "!summon <player> - (!s) Summons a player to you.";
        cp->inform(tmessage);
    }

    if (cp->hasGMRight(gmr_ban)) {
        tmessage = "!ban <time> [m|h|d] <player> - (!b) Bans the player <player> for <time> [m]inutes/[h]ours/[d]ays.";
        cp->inform(tmessage);
    }

    if (cp->hasGMRight(gmr_settiles)) {
        tmessage = "!tile <tilenumber> - (!t) changes the tile in front of you to the tile with id <tilenumber>.";
        cp->inform(tmessage);
        tmessage = "!turtleon <tile number> - (!ton) Change tiles to tile # when you walk (advanced)";
        cp->inform(tmessage);
        tmessage = "!turtleoff - (!toff) Stop changing tiles as you walk.";
        cp->inform(tmessage);
    }

    if (cp->hasGMRight(gmr_clipping)) {
        tmessage = "!clippingon - (!con) Turn clipping on (Can't walk through walls)";
        cp->inform(tmessage);
        tmessage = "!clippingoff - (!coff) Turn clipping off (Walk anywhere)";
        cp->inform(tmessage);
    }

    if (cp->hasGMRight(gmr_save)) {
        tmessage = "!playersave - (!ps) saves all players to the database.";
        cp->inform(tmessage);
        tmessage = "!mapsave - Saves the map after changes where made.";
        cp->inform(tmessage);
    }

    if (cp->hasGMRight(gmr_reload)) {
        tmessage = "!set_spawn <true|false> - activates/deactivates the spawning of monsters.";
        cp->inform(tmessage);
        tmessage = "!reloaddefinitions - (!rd) reloads all datas without spawnpoints, so no new monsters are spawned. (deactivated)";
        cp->inform(tmessage);
        tmessage = "!nuke - kills all Monster on the map (to clean the map after a spawn reload).";
        cp->inform(tmessage);
        tmessage = "!fullreload - (!fr) reloads all database tables";
        cp->inform(tmessage);
    }

    if (cp->hasGMRight(gmr_import)) {
        tmessage = "!importmaps <x> <y> <z> - Imports new maps from the Editor to the location.";
        cp->inform(tmessage);
        tmessage = "create_area <x> <y> <z> <width> <height> <tile> - Creates a new map at <x> <y> <z> with the dimensions <height> <width> and filled with <tile>.";
        cp->inform(tmessage);
        tmessage = "!exportmaps - Exports the current maps.";
        cp->inform(tmessage);
    }

    if (cp->hasGMRight(gmr_loginstate)) {
        tmessage = "!nologin <true|false> - changes the login state, with true only gm's can log in.";
        cp->inform(tmessage);
    }

    if (cp->hasGMRight(gmr_visible)) {
        tmessage = "!makeinvisible - (!mi) makes you invisible for other chars. NOT FOR MONSTERS.";
        cp->inform(tmessage);
        tmessage = "!makevisible - (!mv) makes you visible if you are invisible.";
        cp->inform(tmessage);
    }

    if (cp->hasGMRight(gmr_forcelogout)) {
        tmessage = "!kickall - Kicks all players out of the game.";
        cp->inform(tmessage);
        tmessage = "!kick <playerid> - Kicks the player with the id out of the game.";
        cp->inform(tmessage);
    }
}

//! parse GMCommands of the form !<string1> <string2> and process them
bool World::parseGMCommands(Player *cp, const std::string &text) {

    // did we find a command?
    bool done = false;

    // use a regexp to match for commands...
    regex_t expression;
    regcomp(&expression, "^!([^ ]+) ?(.*)?$",REG_ICASE|REG_EXTENDED);
    regmatch_t matches[3];

    if (regexec(&expression, text.c_str(), 3, matches, 0) == 0) {
        // we found something...
        CommandIterator it = GMCommands.find(text.substr(matches[1].rm_so, matches[1].rm_eo-1));

        // do we have a matching command?
        if (it != GMCommands.end()) {
            if (matches[2].rm_so != -1) { // !bla something
                (it->second)(this, cp, text.substr(matches[2].rm_so));
            } else { // !bla
                (it->second)(this, cp, "");
            }

            done = true;
        }
    }

    regfree(&expression);

    return done;

}

extern MonsterTable *MonsterDescriptions;

void reportError(Player *cp, std::string msg) {
    std::cerr << "ERROR: " << msg << std::endl;
    cp->inform("ERROR: " + msg);
}

void reportScriptError(Player *cp, std::string serverscript, std::string what) {
    reportError(cp, "Failed to reload server." + serverscript + ": " + what);
}

void reportTableError(Player *cp, std::string dbtable) {
    reportError(cp, "Failed to reload DB table: " + dbtable);
}


bool World::reload_defs(Player *cp) {
    if (!cp->hasGMRight(gmr_reload)) {
        return false;
    }

    sendMessageToAllPlayers("### The server is reloading, this may cause some lag ###");

    bool ok = true;

    CommonObjectTable *CommonItems_temp = 0;
    NamesObjectTable *ItemNames_temp = 0;
    WeaponObjectTable *WeaponItems_temp = 0;
    ArmorObjectTable *ArmorItems_temp = 0;
    ContainerObjectTable *ContainerItems_temp = 0;
    TilesModificatorTable *TilesModItems_temp = 0;
    MonsterTable *MonsterDescriptions_temp = 0;
    TilesTable *Tiles_temp = 0;
    QuestTable *Quests_temp = 0;
    SpellTable *Spells_temp = 0;
    TriggerTable *Trigger_temp = 0;
    MonsterAttackTable *MonsterAttacks_temp = 0;
    NaturalArmorTable *NaturalArmors_temp = 0;
    ScheduledScriptsTable *ScheduledScripts_temp = 0;
    LongTimeEffectTable *LongTimeEffects_temp = 0;
    RaceSizeTable *RaceSizes_temp = 0;

    if (ok) {
        Quests_temp = new QuestTable();

        if (Quests_temp == NULL || !Quests_temp->isDataOK()) {
            reportTableError(cp, "quests");
            ok = false;
        }
    }

    if (ok) {
        RaceSizes_temp = new RaceSizeTable();

        if (RaceSizes_temp == NULL || !RaceSizes_temp->isDataOk()) {
            reportTableError(cp, "raceattr");
            ok = false;
        }
    }

    if (ok) {
        QuestNodeTable::getInstance()->reload();
    }

    if (ok) {
        CommonItems_temp = new CommonObjectTable();

        if (CommonItems_temp == NULL || !CommonItems_temp->dataOK()) {
            reportTableError(cp, "common");
            ok = false;
        }
    }

    if (ok) {
        ItemNames_temp = new NamesObjectTable();

        if (ItemNames_temp == NULL || !ItemNames_temp->dataOK()) {
            reportTableError(cp, "itemname");
            ok = false;
        }
    }

    if (ok) {
        WeaponItems_temp = new WeaponObjectTable();

        if (WeaponItems_temp == NULL || !WeaponItems_temp->dataOK()) {
            reportTableError(cp, "weapon");
            ok = false;
        }
    }

    if (ok) {
        ArmorItems_temp = new ArmorObjectTable();

        if (ArmorItems_temp == NULL || !ArmorItems_temp->dataOK()) {
            reportTableError(cp, "armor");
            ok = false;
        }
    }

    if (ok) {
        ContainerItems_temp = new ContainerObjectTable();

        if (ContainerItems_temp == NULL || !ContainerItems_temp->dataOK()) {
            reportTableError(cp, "container");
            ok = false;
        }
    }

    if (ok) {
        TilesModItems_temp = new TilesModificatorTable();

        if (TilesModItems_temp == NULL || !TilesModItems_temp->dataOK()) {
            reportTableError(cp, "tilesmodificators");
            ok = false;
        }
    }

    if (ok) {
        Tiles_temp = new TilesTable();

        if (Tiles_temp == NULL || !Tiles_temp->dataOK()) {
            reportTableError(cp, "tiles");
            ok = false;
        }
    }

    if (ok) {
        MonsterDescriptions_temp = new MonsterTable();

        if (MonsterDescriptions_temp == NULL || !MonsterDescriptions_temp->dataOK()) {
            reportTableError(cp, "monster");
            ok = false;
        }
    }

    if (ok) {
        Spells_temp = new SpellTable();

        if (Spells_temp == NULL || !Spells_temp->isDataOK()) {
            reportTableError(cp, "spells");
            ok = false;
        }
    }

    if (ok) {
        Trigger_temp = new TriggerTable();

        if (Trigger_temp == NULL || !Trigger_temp->isDataOK()) {
            reportTableError(cp, "triggerfields");
            ok = false;
        }
    }

    if (ok) {
        LongTimeEffects_temp = new LongTimeEffectTable();

        if (LongTimeEffects_temp == NULL || !LongTimeEffects->dataOK()) {
            reportTableError(cp, "longtimeeffects");
            ok = false;
        }
    }

    if (ok) {
        MonsterAttacks_temp = new MonsterAttackTable();

        if (MonsterAttacks_temp == NULL || !MonsterAttacks_temp->isDataOk()) {
            reportTableError(cp, "monsterattack");
            ok = false;
        }
    }

    if (ok) {
        NaturalArmors_temp = new NaturalArmorTable();

        if (NaturalArmors_temp == NULL || !NaturalArmors_temp->isDataOk()) {
            reportTableError(cp, "naturalarmor");
            ok = false;
        }
    }

    if (ok) {
        std::cerr << "Attempting to reload Scheduler" << std::endl;
        ScheduledScripts_temp = new ScheduledScriptsTable();
        std::cerr << "Created new Scheduler" << std::endl;

        if (ScheduledScripts_temp == NULL || !ScheduledScripts_temp->dataOK()) {
            reportTableError(cp, "scheduledscripts");
            ok = false;
        }
    }

    if (!ok) {
        if (Quests_temp != NULL) {
            delete Quests_temp;
        }

        if (CommonItems_temp != NULL) {
            delete CommonItems_temp;
        }

        if (ItemNames_temp != NULL) {
            delete ItemNames_temp;
        }

        if (WeaponItems_temp != NULL) {
            delete WeaponItems_temp;
        }

        if (ArmorItems_temp != NULL) {
            delete ArmorItems_temp;
        }

        if (ContainerItems_temp != NULL) {
            delete ContainerItems_temp;
        }

        if (TilesModItems_temp != NULL) {
            delete TilesModItems_temp;
        }

        if (Tiles_temp != NULL) {
            delete Tiles_temp;
        }

        if (MonsterDescriptions_temp != NULL) {
            delete MonsterDescriptions_temp;
        }

        if (Spells_temp != NULL) {
            delete Spells_temp;
        }

        if (Trigger_temp != NULL) {
            delete Trigger_temp;
        }

        if (NaturalArmors_temp != NULL) {
            delete NaturalArmors_temp;
        }

        if (MonsterAttacks_temp != NULL) {
            delete MonsterAttacks_temp;
        }

        if (ScheduledScripts_temp != NULL) {
            delete ScheduledScripts_temp;
        }

        if (LongTimeEffects_temp != NULL) {
            delete LongTimeEffects_temp;
        }

        if (RaceSizes_temp != NULL) {
            delete RaceSizes_temp;
        }

        //if (ScriptVar_temp != NULL)
        //    delete ScriptVar_temp;

    } else {
        // if everything went well, delete old tables and set up new tables
        //Mutex für login logout sperren so das aktuell keiner mehr einloggen kann
        PlayerManager::get()->setLoginLogout(true);
        delete CommonItems;
        CommonItems = CommonItems_temp;
        delete ItemNames;
        ItemNames = ItemNames_temp;
        delete WeaponItems;
        WeaponItems = WeaponItems_temp;
        delete ArmorItems;
        ArmorItems = ArmorItems_temp;
        delete ContainerItems;
        ContainerItems = ContainerItems_temp;
        delete TilesModItems;
        TilesModItems = TilesModItems_temp;
        delete Tiles;
        Tiles = Tiles_temp;
        delete Quests;
        Quests = Quests_temp;
        delete MonsterDescriptions;
        MonsterDescriptions = MonsterDescriptions_temp;
        delete Spells;
        Spells = Spells_temp;
        delete Triggers;
        Triggers = Trigger_temp;
        delete NaturalArmors;
        NaturalArmors = NaturalArmors_temp;
        delete MonsterAttacks;
        MonsterAttacks = MonsterAttacks_temp;
        delete scheduledScripts;
        scheduledScripts = ScheduledScripts_temp;
        delete RaceSizes;
        RaceSizes = RaceSizes_temp;
        delete LongTimeEffects;
        LongTimeEffects = LongTimeEffects_temp;
        // delete scriptVariables;
        // scriptVariables = ScriptVar_temp;
        //Mutex entsperren.
        PlayerManager::get()->setLoginLogout(false);

        //Reload the standard Fighting script
        try {
            boost::shared_ptr<LuaWeaponScript> tmpScript(new LuaWeaponScript("server.standardfighting"));
            standardFightingScript = tmpScript;
        } catch (ScriptException &e) {
            reportScriptError(cp, "standardfighting", e.what());
            ok = false;
        }

        try {
            boost::shared_ptr<LuaLookAtPlayerScript>tmpScript(new LuaLookAtPlayerScript("server.playerlookat"));
            lookAtPlayerScript = tmpScript;
        } catch (ScriptException &e) {
            reportScriptError(cp, "playerlookat", e.what());
            ok = false;
        }

        try {
            boost::shared_ptr<LuaLookAtItemScript>tmpScript(new LuaLookAtItemScript("server.itemlookat"));
            lookAtItemScript = tmpScript;
        } catch (ScriptException &e) {
            reportScriptError(cp, "itemlookat", e.what());
            ok = false;
        }

        try {
            boost::shared_ptr<LuaPlayerDeathScript>tmpScript(new LuaPlayerDeathScript("server.playerdeath"));
            playerDeathScript = tmpScript;
        } catch (ScriptException &e) {
            reportScriptError(cp, "playerdeath", e.what());
            ok = false;
        }

        try {
            boost::shared_ptr<LuaLoginScript>tmpScript(new LuaLoginScript("server.login"));
            loginScript = tmpScript;
        } catch (ScriptException &e) {
            reportScriptError(cp, "login", e.what());
            ok = false;
        }

        try {
            boost::shared_ptr<LuaLogoutScript>tmpScript(new LuaLogoutScript("server.logout"));
            logoutScript = tmpScript;
        } catch (ScriptException &e) {
            reportScriptError(cp, "logout", e.what());
            ok = false;
        }

        try {
            boost::shared_ptr<LuaLearnScript>tmpScript(new LuaLearnScript("server.learn"));
            learnScript = tmpScript;
        } catch (ScriptException &e) {
            reportScriptError(cp, "learn", e.what());
            ok = false;
        }

        try {
            boost::shared_ptr<LuaDepotScript>tmpScript(new LuaDepotScript("server.depot"));
            depotScript = tmpScript;
        } catch (ScriptException &e) {
            reportScriptError(cp, "depot", e.what());
            ok = false;
        }

    }


    if (ok) {
        cp->inform(" *** Definitions reloaded *** ");
    } else {
        cp->inform("CRITICAL ERROR: Failure while reloading definitions");
    }

    return ok;
}


bool World::reload_tables(Player *cp) {

    scriptVariables->save();

    LuaScript::shutdownLua();

    bool ok = reload_defs(cp);

    if (ok) {
        // reload respawns
        initRespawns();

        //reload NPC's
        initNPC();

        for (auto it = Players.begin(); it < Players.end(); ++it) {
            (*it)->sendCompleteQuestProgress();
        }

        try {
            boost::shared_ptr<LuaReloadScript> tmpScript(new LuaReloadScript("server.reload"));
            tmpScript->onReload();
        } catch (ScriptException &e) {
            reportScriptError(cp, "reload", e.what());
        }

    }

    return ok;
}


// enable/disable spawnpoints
void set_spawn_command(World *, Player *player, const std::string &in) {
    if (!player->hasGMRight(gmr_reload)) {
        return;
    }

    std::cout << "set spawn to '" << in << "' by: " << player->name << std::endl;
    configOptions["do_spawn"] = in;
}


void import_maps_command(World *world, Player *player, const std::string &param) {
    if (!player->hasGMRight(gmr_import)) {
        return;
    }

    world->load_maps();
}

// create a new area starting at x,y,z with dimension w,h, filltile ft (create_area x y z w h ft)
void create_area_command(World *world, Player *player,const std::string &params) {
    if (!player->hasGMRight(gmr_import)) {
        return;
    }

    std::stringstream ss(params);
    int x,y,z,w,h, filltile;
    x=y=z=w=h=filltile=-65535;
    ss >> x;
    ss >> y;
    ss >> z;
    ss >> w;
    ss >> h;
    ss >> filltile;

    if (x==-65535 || y == -65535 || z == -65535 || w < 1 || h < 1 || filltile < 0) {
        std::cout << "error in create_area_command issued by " << player->name << "!" << std::endl;
        std::cout << "positions: " << x << "\t" << y << '\t' << z << '\t' << w << '\t' << h << '\t' << std::endl;
        return;
    }

    WorldMap::map_t tempmap(new Map(w,h));
    tempmap->Init(x, y, z);

    Field *tempf;

    for (int _x=0; _x<w; ++_x)
        for (int _y=0; _y<h; ++_y) {
            if (tempmap->GetPToCFieldAt(tempf, _x+x, _y+y)) {
                tempf->setTileId(filltile);
                tempf->updateFlags();
            } else {
                std::cerr << "error in create map: " << x << " " << y << " " << z << " " << _x << " " << _y << " " << filltile << std::endl;
            }

        }

    world->maps.InsertMap(tempmap);

    std::string tmessage = "map inserted.";
    player->inform(tmessage);
    std::cerr << "Map created by " << player->name << " on " << x << " - " << y << " - " << z << " with w: " << w << " h: " << h << "ft: " << filltile << std::endl;

}

void set_login(World *world, Player *player, const std::string &st) {
    if (!player->hasGMRight(gmr_loginstate)) {
        return;
    }

    configOptions["disable_login"] = st;
    std::cout << "nologin set to " << st << std::endl;
    std::string tmessage = "nologin set to: " + st;
    player->inform(tmessage);
}

bool World::exportMaps(Player *cp) {
    if (!cp->hasGMRight(gmr_import)) {
        return false;
    }

    std::string exportDir = directory + std::string(MAPDIR) + "export/";
    return maps.exportTo(exportDir);
}

void World::removeTeleporter(Player *cp, const std::string &ts) {
    if (!cp->hasGMRight(gmr_warpfields)) {
        return;
    }

    position teleport;
    char *tokenize = new char[ ts.length() + 1 ];

    strcpy(tokenize, ts.c_str());
    std::cout << "Tokenizing " << tokenize << std::endl;
    char *thistoken;

    if ((thistoken = strtok(tokenize, " ,")) != NULL) {
        if (ReadField(thistoken, teleport.x)) {
            if ((thistoken = strtok(NULL, " ,")) != NULL) {
                if (ReadField(thistoken, teleport.y)) {
                    if ((thistoken = strtok(NULL, " ,")) != NULL) {
                        if (ReadField(thistoken, teleport.z)) {
                            if (removeWarpField(teleport)) {
                                std::string tmessage = "*** Warp Field deleted! ***";
                                cp->inform(tmessage);
                            } else {
                                std::string tmessage = "*** Warp Field *NOT* deleted! ***";
                                cp->inform(tmessage);
                            };
                        }
                    }
                }
            }
        }
    }

    delete [] tokenize;
}

void World::showWarpFieldsInRange(Player *cp, const std::string &ts) {
    if (!cp->hasGMRight(gmr_warpfields)) {
        return;
    }

    short int range = 0;

    if (ReadField(ts.c_str(), range)) {
        std::vector< boost::shared_ptr< position > > warpfieldsinrange;

        if (findWarpFieldsInRange(cp->pos, range, warpfieldsinrange)) {
            std::vector< boost::shared_ptr< position > >::iterator it;
            std::string message;
            cp->inform("Start list of warpfields:");

            for (it = warpfieldsinrange.begin(); it != warpfieldsinrange.end(); ++it) {
                position target;
                GetField(**it)->GetWarpField(target);
                message = "Warpfield at (x,y,z) " + stream_convert<std::string>((*it)->x) + "," + stream_convert<std::string> ((*it)->y) + "," + stream_convert<std::string>((*it)->z) + " Target (x,y,z) : " + stream_convert<std::string>(target.x) + "," + stream_convert<std::string>(target.y) + "," + stream_convert<std::string>(target.z);
                cp->inform(message);
            }

            cp->inform("End list of warpfields.");
        }
    }
}
