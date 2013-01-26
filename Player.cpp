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


#define _XOPEN_SOURCE
#include <unistd.h>

#include "db/ConnectionManager.hpp"
#include "Player.hpp"
#include <sstream>
#include <memory>
#include "tuningConstants.hpp"
#include "data/ContainerObjectTable.hpp"
#include "data/CommonObjectTable.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "Field.hpp"
#include "World.hpp"
#include "netinterface/NetInterface.hpp"
#include "netinterface/BasicClientCommand.hpp"
//for login command
#include "netinterface/protocol/ClientCommands.hpp"
#include "netinterface/protocol/ServerCommands.hpp"
//#include "playersave.hpp"
#include "Random.hpp"
#include "SchedulerTaskClasses.hpp"
#include "netinterface/protocol/BBIWIServerCommands.hpp"
#include "Logger.hpp"
#include "tvector.hpp"
#include "PlayerManager.hpp"

#include "db/Connection.hpp"
#include "db/ConnectionManager.hpp"
#include "db/DeleteQuery.hpp"
#include "db/InsertQuery.hpp"
#include "db/SelectQuery.hpp"
#include "db/UpdateQuery.hpp"
#include "db/Result.hpp"

#include "dialog/InputDialog.hpp"
#include "dialog/MessageDialog.hpp"
#include "dialog/MerchantDialog.hpp"
#include "dialog/SelectionDialog.hpp"

//#define PLAYER_MOVE_DEBUG

Player::Player(boost::shared_ptr<NetInterface> newConnection) throw(Player::LogoutException)
    : Character(), onlinetime(0), Connection(newConnection),
      turtleActive(false), clippingActive(true), admin(false), dialogCounter(0) {
#ifdef Player_DEBUG
    std::cout << "Player Konstruktor Start" << std::endl;
#endif

    screenwidth = 0;
    screenheight = 0;
    character = player;
    SetAlive(true);
    SetMovement(walk);
    prefix="";
    suffix="";

    time(&lastaction);

    ltAction = new LongTimeAction(this, _world);

    // first check if we have a valid client

    boost::shared_ptr<BasicClientCommand> cmd = Connection->getCommand();

    if ((cmd == NULL) || cmd->getDefinitionByte() != C_LOGIN_TS) {
        throw LogoutException(UNSTABLECONNECTION);
    }

    unsigned short int clientversion = boost::dynamic_pointer_cast<LoginCommandTS>(cmd)->clientVersion;
    name = boost::dynamic_pointer_cast<LoginCommandTS>(cmd)->loginName;
    pw = boost::dynamic_pointer_cast<LoginCommandTS>(cmd)->passwort;
    // set acceptable client version...
    unsigned short acceptVersion;
    std::stringstream stream;
    stream << configOptions["clientversion"];
    stream >> acceptVersion;
    monitoringClient = false;

    if (clientversion == 200) {
        monitoringClient = true;
    } else if (clientversion != acceptVersion) {
        std::cerr << "old client: " << clientversion << " != " << acceptVersion << std::endl;
        throw LogoutException(OLDCLIENT);
    }

    // client version seems to be ok... now find out username/password he supplied...

    if (name == "" || pw == "") {
        // no name/password retrieved... kick him...
        std::cerr << "no name/pwd recvd" << std::endl;
        throw LogoutException(WRONGPWD);
    }

    // player already online? if we don't use the monitoring client
    if (!monitoringClient && (_world->Players.find(name) || PlayerManager::get()->findPlayer(name))) {
        std::cout << "double login by " << name  << std::endl;
        throw LogoutException(DOUBLEPLAYER);
    }

    std::cerr << "checking login data for: " << name << std::endl;
    // next check if username/password are ok
    check_logindata();
    std::cerr << "checking login data done" << std::endl;

    if (status == 7) {
        // no skill package chosen... kick him...
        std::cerr << "no skill package chosen" << std::endl;
        throw LogoutException(NOSKILLS);
    }

    if (!loadGMFlags()) {
        throw LogoutException(UNSTABLECONNECTION);    //Fehler beim Laden der GM daten
    }

    std::cerr << "error loading gm flags" << std::endl;


    if (!hasGMRight(gmr_allowlogin) && configOptions["disable_login"] == "true") {
        throw Player::LogoutException(SERVERSHUTDOWN);
    }

    std::cerr << "no gm rights" << std::endl;

    if (monitoringClient && !hasGMRight(gmr_ban)) {
        throw Player::LogoutException(NOACCOUNT);
    }

    last_ip = Connection->getIPAdress();
    std::cerr << "write last IP" << std::endl;

    //we dont want to add more if we have a monitoring client
    if (monitoringClient) {
        std::cout<<"login monitoring client: "<<name<<std::endl;
        return;
    }

    // now load inventory...
    if (!load()) {
        throw LogoutException(ORRUPTDATA);
    }

    std::cerr << "loading inventory done" << std::endl;

#ifdef Player_DEBUG
    std::cout << "Player Konstruktor Ende" << std::endl;
#endif

}

void Player::login() throw(Player::LogoutException) {
    // find a position for our player...
    short int x,y,z;
    bool target_position_found;
    Field *target_position;

    // try to find a targetposition near the logout place...
    x = pos.x;
    y = pos.y;
    z = pos.z;
    target_position_found = _world->findEmptyCFieldNear(target_position, x, y, z);

    if (!target_position_found) {
        // move player to startingpoint...
        std::stringstream ssx(configOptions["playerstart_x"]);
        ssx >> x;
        std::stringstream ssy(configOptions["playerstart_y"]);
        ssy >> y;
        std::stringstream ssz(configOptions["playerstart_z"]);
        ssz >> z;

        target_position_found = _world->findEmptyCFieldNear(target_position, x, y, z);
    }

    if (!target_position_found) {
        throw LogoutException(NOPLACE);
    }

    // set player on target field...
    pos.x=x;
    pos.y=y;
    pos.z=z;
    target_position->SetPlayerOnField(true);

    sendWeather(_world->weather);

    // send player login data
    boost::shared_ptr<BasicServerCommand> cmd(new IdTC(id));
    Connection->addCommand(cmd);
    // position
    cmd.reset(new SetCoordinateTC(pos));
    Connection->addCommand(cmd);

    effects->load();

    //send the basic data to the monitoring client
    cmd.reset(new BBPlayerTC(id, name, pos.x, pos.y,pos.z));
    _world->monitoringClientList->sendCommand(cmd);

    // send weather and time before sending the map, to display everything correctly from the start
    _world->sendIGTime(this);
    _world->sendWeather(this);

    // send std::map, items and chars around...
    sendFullMap();
    _world->sendAllVisibleCharactersToPlayer(this , true);

    // sent inventory
    for (unsigned short int i = 0; i < MAX_BELT_SLOTS + MAX_BODY_ITEMS; ++i) {
        sendCharacterItemAtPos(i);
    }

    sendAllSkills();
    // notify other players
    _world->sendCharacterMoveToAllVisiblePlayers(this, NORMALMOVE, 4);
    // additional nop info
    _world->sendSpinToAllVisiblePlayers(this);
    cmd.reset(new PlayerSpinTC(faceto, id));
    Connection->addCommand(cmd);

    // send attributes
    sendAttrib(hitpoints);
    sendAttrib(mana);
    sendAttrib(foodlevel);
    sendAttrib(sex);
    sendAttrib(age);
    sendAttrib(weight);
    sendAttrib(height);
    sendAttrib(attitude);
    sendAttrib(luck);
    sendAttrib(strength);
    sendAttrib(dexterity);
    sendAttrib(constitution);
    sendAttrib(intelligence);
    sendAttrib(perception);
    sendAttrib(willpower);
    sendAttrib(essence);
    sendAttrib(agility);
    //end of changes


    // send magic skills
    sendMagicFlags(magic.type);

    time(&logintime);
    time(&lastkeepalive);

    if ((LoadWeight() * 100) / maxLoadWeight() > 75) {
        setEncumberedSent(true);
        std::string german = "Deine Last bremst dich.";
        std::string english = "Your burden slows you down.";
        informLua(german, english);
    } else {
        setEncumberedSent(false);
    }

    time(&lastsavetime);
}

unsigned short int Player::getScreenRange() const {
    return (screenwidth > screenheight) ? 2*screenwidth : 2*screenheight;
}

void Player::openShowcase(Container *container, bool carry) {
    for (auto it = showcases.cbegin(); it != showcases.cend(); ++it) {
        if (it->second->contains(container)) {
            boost::shared_ptr<BasicServerCommand>cmd(new UpdateShowCaseTC(it->first, container->getSlotCount(), container->getItems()));
            Connection->addCommand(cmd);
            return;
        }
    }

    if (showcases.size() < MAXSHOWCASES) {
        while (isShowcaseOpen(showcaseCounter)) {
            ++showcaseCounter;
        }

        showcases[showcaseCounter] = new Showcase(container, carry);

        boost::shared_ptr<BasicServerCommand>cmd(new UpdateShowCaseTC(showcaseCounter, container->getSlotCount(), container->getItems()));
        Connection->addCommand(cmd);
    } else {
        inform("ERROR: Unable to open more than 100 containers.");
    }
}

void Player::updateShowcase(Container *container) const {
    for (auto it = showcases.cbegin(); it != showcases.cend(); ++it) {
        if (it->second->contains(container)) {
            boost::shared_ptr<BasicServerCommand>cmd(new UpdateShowCaseTC(it->first, container->getSlotCount(), container->getItems()));
            Connection->addCommand(cmd);
        }
    }
}

bool Player::isShowcaseOpen(uint8_t showcase) const {
    return showcases.find(showcase) != showcases.cend();
}

bool Player::isShowcaseOpen(Container *container) const {
    for (auto it = showcases.cbegin(); it != showcases.cend(); ++it) {
        if (it->second->contains(container)) {
            return true;
        }
    }

    return false;
}

bool Player::isShowcaseInInventory(uint8_t showcase) const {
    if (isShowcaseOpen(showcase)) {
        return showcases.at(showcase)->inInventory();
    }

    return false;
}

uint8_t Player::getShowcaseId(Container *container) const {
    for (auto it = showcases.cbegin(); it != showcases.cend(); ++it) {
        if (it->second->contains(container)) {
            return it->first;
        }
    }

    throw std::logic_error("container has no showcase!");
}

Container *Player::getShowcaseContainer(uint8_t showcase) const {
    auto it = showcases.find(showcase);

    if (it != showcases.end()) {
        return it->second->getContainer();
    }

    return 0;
}

void Player::closeShowcase(uint8_t showcase) {
    if (isShowcaseOpen(showcase)) {
        delete showcases[showcase];
        showcases.erase(showcase);
        boost::shared_ptr<BasicServerCommand>cmd(new ClearShowCaseTC(showcase));
        Connection->addCommand(cmd);
    }
}

void Player::closeShowcase(Container *container) {
    for (auto it = showcases.cbegin(); it != showcases.cend();) {
        if (it->second->contains(container)) {
            delete it->second;
            boost::shared_ptr<BasicServerCommand>cmd(new ClearShowCaseTC(it->first));
            Connection->addCommand(cmd);
            it = showcases.erase(it);
        } else {
            ++it;
        }
    }
}

void Player::closeOnMove() {
    closeAllShowcasesOfMapContainers();
    closeDialogsOnMove();
}

void Player::closeAllShowcasesOfMapContainers() {
    for (auto it = showcases.cbegin(); it != showcases.cend();) {
        if (!it->second->inInventory()) {
            delete it->second;
            boost::shared_ptr<BasicServerCommand> cmd(new ClearShowCaseTC(it->first));
            Connection->addCommand(cmd);
            it = showcases.erase(it);
        } else {
            ++it;
        }
    }
}

void Player::closeAllShowcases() {
    for (auto it = showcases.cbegin(); it != showcases.cend(); ++it) {
        delete it->second;
        boost::shared_ptr<BasicServerCommand> cmd(new ClearShowCaseTC(it->first));
        Connection->addCommand(cmd);
    }

    showcases.clear();
}

Player::~Player() {
#ifdef Player_DEBUG
    std::cout << "Player Destruktor Start/Ende" << std::endl;
#endif
}

bool Player::VerifyPassword(std::string chkpw) {
    return (pw == chkpw);
}


void Player::sendCharacters() {
    _world->sendAllVisibleCharactersToPlayer(this, true);
}


void Player::sendCharacterItemAtPos(unsigned char cpos) {
    if (cpos < (MAX_BELT_SLOTS + MAX_BODY_ITEMS)) {
        // gltiger Wert
        boost::shared_ptr<BasicServerCommand>cmd(new UpdateInventoryPosTC(cpos, characterItems[cpos].getId(), characterItems[cpos].getNumber()));
        Connection->addCommand(cmd);
    }
}


void Player::sendWeather(WeatherStruct weather) {
    boost::shared_ptr<BasicServerCommand>cmd(new UpdateWeatherTC(weather));
    Connection->addCommand(cmd);
}


void Player::ageInventory() {
    CommonStruct tempCommon;

    for (unsigned char i = 0; i < MAX_BELT_SLOTS + MAX_BODY_ITEMS; ++i) {
        if (characterItems[ i ].getId() != 0) {
            if (!CommonItems->find(characterItems[ i ].getId(), tempCommon)) {
                tempCommon.rotsInInventory=false;
                tempCommon.ObjectAfterRot = characterItems[ i ].getId();
            }

            if (tempCommon.rotsInInventory) {
                if (!characterItems[ i ].survivesAgeing()) {
                    if (characterItems[ i ].getId() != tempCommon.ObjectAfterRot) {
#ifdef Character_DEBUG
                        std::cout << "INV:Ein Item wird umgewandelt von: " << characterItems[ i ].getId() << "  nach: " << tempCommon.ObjectAfterRot << "!\n";
#endif
                        characterItems[ i ].setId(tempCommon.ObjectAfterRot);

                        if (CommonItems->find(tempCommon.ObjectAfterRot, tempCommon)) {
                            characterItems[ i ].setWear(tempCommon.AgeingSpeed);
                        }

                        sendCharacterItemAtPos(i);
                    } else {
#ifdef Character_DEBUG
                        std::cout << "INV:Ein Item wird gel�cht,ID:" << characterItems[ i ].getId() << "!\n";
#endif
                        characterItems[ i ].reset();
                        sendCharacterItemAtPos(i);
                    }

                    // The personal light might have changed!
                    updateAppearanceForAll(true);
                }
            }
        }
    }

    // Inhalt des Rucksacks altern
    if ((characterItems[ BACKPACK ].getId() != 0) && (backPackContents != NULL)) {
        backPackContents->doAge(true);
        updateBackPackView();
    }

    std::map<uint32_t, Container *>::iterator depotIterator;

    for (depotIterator = depotContents.begin(); depotIterator != depotContents.end(); ++depotIterator) {
        if (depotIterator->second != NULL) {
            depotIterator->second->doAge(true);
            updateShowcase(depotIterator->second);
        }
    }

}

void Player::learn(TYPE_OF_SKILL_ID skill, uint32_t actionPoints, uint8_t opponent) {

    uint16_t majorSkillValue = getSkill(skill);
    uint16_t minorSkillValue = getMinorSkill(skill);

    Character::learn(skill, actionPoints, opponent);

    uint16_t newMajorSkillValue = getSkill(skill);
    uint16_t newMinorSkillValue = getMinorSkill(skill);

    if (newMinorSkillValue != minorSkillValue || newMajorSkillValue != majorSkillValue) {
        sendSkill(skill, newMajorSkillValue, newMinorSkillValue);
    }
}


int Player::createItem(Item::id_type id, Item::number_type number, Item::quality_type quality, const luabind::object &data) {
    int temp = Character::createItem(id, number, quality, data);

    for (unsigned char i = 0; i < MAX_BELT_SLOTS + MAX_BODY_ITEMS; ++i) {
        if (characterItems[ i ].getId() != 0) {
            sendCharacterItemAtPos(i);
        }
    }

    updateBackPackView();

    return temp;
}


int Player::_eraseItem(TYPE_OF_ITEM_ID itemid, int count, const luabind::object &data, bool useData) {
    int temp = count;
#ifdef Player_DEBUG
    std::cout << "try to erase in player inventory " << count << " items of type " << itemid << "\n";
#endif

    if ((characterItems[ BACKPACK ].getId() != 0) && (backPackContents != NULL)) {
        temp = backPackContents->_eraseItem(itemid, temp, data, useData);
        updateBackPackView();
#ifdef Player_DEBUG
        std::cout << "eraseItem: nach L�chen im Rucksack noch zu l�chen: " << temp << "\n";
#endif

    }

    if (temp > 0) {
        // BACKPACK als Item erstmal auslassen
        for (unsigned char i = MAX_BELT_SLOTS + MAX_BODY_ITEMS - 1; i > 0; --i) {
            if ((characterItems[ i ].getId() == itemid) && (!useData || characterItems[ i ].hasData(data)) && (temp > 0)) {
                if (temp >= characterItems[ i ].getNumber()) {
                    temp = temp - characterItems[ i ].getNumber();
                    characterItems[ i ].reset();

                    //std::cout<<"Try to find out if it was a two hander!"<<std::endl;
                    if (i == LEFT_TOOL || i == RIGHT_TOOL) {
                        unsigned char offhand = (i==LEFT_TOOL)?RIGHT_TOOL:LEFT_TOOL;

                        if (characterItems[ offhand ].getId() == BLOCKEDITEM) {
                            // delete the occupied slot if the item was a two hander...
                            //std::cout<<"Item was two hander try to delete occopied slot"<<std::endl;
                            characterItems[ offhand ].reset();
                            sendCharacterItemAtPos(offhand);
                        }
                    }
                } else {
                    characterItems[ i ].setNumber(characterItems[ i ].getNumber() - temp);
                    temp = 0;
                }

                sendCharacterItemAtPos(i);
            }
        }

        if (World::get()->getItemStatsFromId(itemid).Brightness > 0) {
            updateAppearanceForAll(true);
        }
    }

#ifdef Player_DEBUG
    std::cout << "eraseItem: am Ende noch zu l�chen: " << temp << "\n";
#endif
    return temp;
}


int Player::eraseItem(TYPE_OF_ITEM_ID itemid, int count) {
    const luabind::object nothing;
    return _eraseItem(itemid, count, nothing, false);
}


int Player::eraseItem(TYPE_OF_ITEM_ID itemid, int count, const luabind::object &data) {
    return _eraseItem(itemid, count, data, true);
}


int Player::increaseAtPos(unsigned char pos, int count) {
    int temp = count;

#ifdef Player_DEBUG
    std::cout << "increaseAtPos " << (short int) pos << " " << count << "\n";
#endif

    if ((pos > 0) && (pos < MAX_BELT_SLOTS + MAX_BODY_ITEMS)) {
        if (weightOK(characterItems[ pos ].getId(), count, NULL)) {

            temp = characterItems[ pos ].getNumber() + count;
#ifdef Player_DEBUG
            std::cout << "temp " << temp << "\n";
#endif
            auto maxStack = characterItems[pos].getMaxStack();

            if (temp > maxStack) {
                characterItems[ pos ].setNumber(maxStack);
                temp = temp - maxStack;
            } else if (temp <= 0) {
                bool updateBrightness = World::get()->getItemStatsFromId(characterItems[ pos ].getId()).Brightness > 0;
                temp = count + characterItems[ pos ].getNumber();
                characterItems[ pos ].reset();

                //L�chen des Occopied Slots
                if (pos == RIGHT_TOOL && characterItems[LEFT_TOOL].getId() == BLOCKEDITEM) {
                    characterItems[LEFT_TOOL].reset();
                    sendCharacterItemAtPos(LEFT_TOOL);
                }

                if (updateBrightness) {
                    updateAppearanceForAll(true);
                }
            } else {
                characterItems[ pos ].setNumber(temp);
                temp = 0;
            }
        }
    }

    sendCharacterItemAtPos(pos);
    return temp;
}

int Player::createAtPos(unsigned char pos, TYPE_OF_ITEM_ID newid, int count) {
    int temp = Character::createAtPos(pos,newid,count);
    sendCharacterItemAtPos(pos);
    return temp;

}

bool Player::swapAtPos(unsigned char pos, TYPE_OF_ITEM_ID newid , uint16_t newQuality) {
    if (Character::swapAtPos(pos, newid, newQuality)) {
        sendCharacterItemAtPos(pos);
        return true;
    }

    return false;
}


void Player::updateBackPackView() {
    if (backPackContents != NULL) {
        updateShowcase(backPackContents);
    }
}


void Player::sendSkill(TYPE_OF_SKILL_ID skill, unsigned short int major, unsigned short int minor) {
    boost::shared_ptr<BasicServerCommand>cmd(new UpdateSkillTC(skill, major, minor));
    Connection->addCommand(cmd);
    cmd.reset(new BBSendSkillTC(id, skill, major, minor));
    _world->monitoringClientList->sendCommand(cmd);
}


void Player::sendAllSkills() {
    for (SKILLMAP::const_iterator ptr = skills.begin(); ptr != skills.end(); ++ptr) {
        if (ptr->second.major>0) {
            sendSkill(ptr->first, ptr->second.major, ptr->second.minor);
        }
    }
}


void Player::sendMagicFlags(int type) {
    if ((type >= 0) && (type < 4)) {
        boost::shared_ptr<BasicServerCommand>cmd(new UpdateMagicFlagsTC(type, magic.flags[ type ]));
        Connection->addCommand(cmd);
    }
}


void Player::sendAttrib(Character::attributeIndex attribute) {
    auto value = getAttribute(attribute);
    boost::shared_ptr<BasicServerCommand> cmd(new UpdateAttribTC(id, attributeStringMap[attribute], value));
    Connection->addCommand(cmd);
    cmd.reset(new BBSendAttribTC(id, attributeStringMap[attribute], value));
    _world->monitoringClientList->sendCommand(cmd);
}


void Player::handleAttributeChange(Character::attributeIndex attribute) {
    Character::handleAttributeChange(attribute);
    sendAttrib(attribute);
}


void Player::startMusic(short int title) {
    boost::shared_ptr<BasicServerCommand>cmd(new MusicTC(title));
    Connection->addCommand(cmd);
}


void Player::defaultMusic() {
    boost::shared_ptr<BasicServerCommand>cmd(new MusicDefaultTC());
    Connection->addCommand(cmd);
}


// Setters and Getters //

unsigned char Player::GetStatus() {
    return status;
}


void Player::SetStatus(unsigned char thisStatus) {
    status = thisStatus;
}


// What time does the status get reset?
time_t Player::GetStatusTime() {
    return statustime;
}


void Player::SetStatusTime(time_t thisStatustime) {
    statustime = thisStatustime;
}


// Who banned/jailed the player?
std::string Player::GetStatusGM() {
    using namespace Database;
    SelectQuery query;
    query.addColumn("chars", "chr_playerid");
    query.addEqualCondition<TYPE_OF_CHARACTER_ID>("chars", "chr_playerid", statusgm);
    query.addServerTable("chars");

    Result result = query.execute();

    std::string statusgmstring;

    if (!result.empty()) {
        const ResultField field = result.front()["chr_playerid"];

        if (!field.is_null()) {
            statusgmstring = field.as<std::string>();
        }
    }

    return statusgmstring;
}


void Player::SetStatusGM(TYPE_OF_CHARACTER_ID thisStatusgm) {
    statusgm = thisStatusgm;
}


// Why where they banned/jailed?
std::string Player::GetStatusReason() {
    return statusreason;
}


void Player::SetStatusReason(std::string thisStatusreason) {
    statusreason = thisStatusreason;
}

// World Map Turtle Graphics
void Player::setTurtleActive(bool tturtleActive) {
    turtleActive = tturtleActive;
    setClippingActive(!tturtleActive);
}


void Player::setClippingActive(bool tclippingActive) {
    clippingActive = tclippingActive;
}


bool Player::getTurtleActive() {
    return turtleActive;
}


bool Player::getClippingActive() {
    return clippingActive;
}


void Player::setTurtleTile(unsigned char tturtletile) {
    turtletile = tturtletile;
}


unsigned char Player::getTurtleTile() {
    return turtletile;
}


void Player::setAdmin(uint32_t tAdmin) {
    admin = tAdmin;
}


bool Player::isAdmin() {
    return (admin>0 && !hasGMRight(gmr_isnotshownasgm));
}


void Player::setEncumberedSent(bool tEncumberedSent) {
    encumberedSent = tEncumberedSent;
}


bool Player::wasEncumberedSent() {
    return encumberedSent;
}


void Player::setUnconsciousSent(bool tUnconsciousSent) {

    unconsciousSent = tUnconsciousSent;
}


bool Player::wasUnconsciousSent() {
    return unconsciousSent;
}

void Player::check_logindata() throw(Player::LogoutException) {
    Database::PConnection connection = Database::ConnectionManager::getInstance().getConnection();

    try {
        Database::SelectQuery charQuery(connection);
        charQuery.addColumn("chars", "chr_playerid");
        charQuery.addColumn("chars", "chr_accid");
        charQuery.addColumn("chars", "chr_status");
        charQuery.addColumn("chars", "chr_statustime");
        charQuery.addColumn("chars", "chr_onlinetime");
        charQuery.addColumn("chars", "chr_lastsavetime");
        charQuery.addColumn("chars", "chr_sex");
        charQuery.addColumn("chars", "chr_race");
        charQuery.addColumn("chars", "chr_prefix");
        charQuery.addColumn("chars", "chr_suffix");
        charQuery.addEqualCondition<std::string>("chars", "chr_name", name);
        charQuery.addServerTable("chars");

        Database::Result charResult = charQuery.execute();

        if (charResult.empty()) {
            throw LogoutException(NOCHARACTERFOUND);
        }

        Database::ResultTuple charRow = charResult.front();

        TYPE_OF_CHARACTER_ID account_id;

        id = charRow["chr_playerid"].as<TYPE_OF_CHARACTER_ID>();
        account_id = charRow["chr_accid"].as<TYPE_OF_CHARACTER_ID>();
        status = charRow["chr_status"].as<uint16_t>();

        if (!charRow["chr_statustime"].is_null()) {
            statustime = charRow["chr_statustime"].as<time_t>();
        }

        onlinetime = charRow["chr_onlinetime"].as<time_t>();
        lastsavetime = charRow["chr_lastsavetime"].as<time_t>();
        setAttribute(Character::sex, charRow["chr_sex"].as<uint16_t>());
        race = (Character::race_type) charRow["chr_race"].as<uint16_t>();

        if (!charRow["chr_prefix"].is_null()) {
            prefix = charRow["chr_prefix"].as<std::string>();
        }

        if (!charRow["chr_suffix"].is_null()) {
            suffix = charRow["chr_suffix"].as<std::string>();
        }

        // first we check the status since we already retrieved it
        switch (status) {
        case BANNED:
            // player banned forever
            throw LogoutException(BYGAMEMASTER);

        case BANNEDFORTIME:
            // player banned for some time...
            // check if time is up?
            time_t time_now;
            time(&time_now);

            if (time_now > statustime) {
                status=statustime=0;
            } else {
                throw LogoutException(BYGAMEMASTER);
            }

            break;

        case WAITINGVALIDATION:
            // player not approved yet
            throw LogoutException(NOACCOUNT);
        }

        // next we get the infos for the account and check if the account is active

        Database::SelectQuery accQuery(connection);
        accQuery.addColumn("account", "acc_passwd");
        accQuery.addColumn("account", "acc_lang");
        accQuery.addColumn("account", "acc_state");
        accQuery.addEqualCondition("account", "acc_id", account_id);
        accQuery.addAccountTable("account");

        Database::Result accResult = accQuery.execute();

        if (accResult.empty()) {
            throw LogoutException(NOACCOUNT);
        }

        Database::ResultTuple accRow = accResult.front();

        int acc_state;
        Language::LanguageType mother_tongue;
        std::string real_pwd;

        real_pwd = accRow["acc_passwd"].as<std::string>();
        mother_tongue = (Language::LanguageType)(accRow["acc_lang"].as<uint16_t>());
        acc_state = accRow["acc_state"].as<uint16_t>();

        setPlayerLanguage(mother_tongue);

        // check if account is active
        if (acc_state < 3) { // TODO how is acc_state defined??
            throw LogoutException(NOACCOUNT);
        }

        if (acc_state > 3) {
            throw LogoutException(BYGAMEMASTER);
        }

        // check password
        if (std::string(crypt(pw.c_str(),"$1$illarion1")) != real_pwd) {
            std::cerr << "*** ";
            time_t acttime = time(NULL);
            std::string attempttime = ctime(&acttime);
            attempttime[attempttime.size()-1] = ':';
            std::cerr << attempttime;
            std::cerr << " Player '" << name << "' sent PWD '" << pw
                      << "' but needs  '" << real_pwd << "' ip: "
                      << Connection->getIPAdress() << std::endl;

            throw LogoutException(WRONGPWD);
        }

        character = Character::player;

        Database::SelectQuery playerQuery(connection);
        playerQuery.addColumn("player", "ply_posx");
        playerQuery.addColumn("player", "ply_posy");
        playerQuery.addColumn("player", "ply_posz");
        playerQuery.addColumn("player", "ply_faceto");
        playerQuery.addColumn("player", "ply_age");
        playerQuery.addColumn("player", "ply_weight");
        playerQuery.addColumn("player", "ply_body_height");
        playerQuery.addColumn("player", "ply_hitpoints");
        playerQuery.addColumn("player", "ply_mana");
        playerQuery.addColumn("player", "ply_attitude");
        playerQuery.addColumn("player", "ply_luck");
        playerQuery.addColumn("player", "ply_strength");
        playerQuery.addColumn("player", "ply_dexterity");
        playerQuery.addColumn("player", "ply_constitution");
        playerQuery.addColumn("player", "ply_agility");
        playerQuery.addColumn("player", "ply_intelligence");
        playerQuery.addColumn("player", "ply_perception");
        playerQuery.addColumn("player", "ply_willpower");
        playerQuery.addColumn("player", "ply_essence");
        playerQuery.addColumn("player", "ply_foodlevel");
        playerQuery.addColumn("player", "ply_lifestate");
        playerQuery.addColumn("player", "ply_magictype");
        playerQuery.addColumn("player", "ply_magicflagsmage");
        playerQuery.addColumn("player", "ply_magicflagspriest");
        playerQuery.addColumn("player", "ply_magicflagsbard");
        playerQuery.addColumn("player", "ply_magicflagsdruid");
        playerQuery.addColumn("player", "ply_poison");
        playerQuery.addColumn("player", "ply_mental_capacity");
        playerQuery.addColumn("player", "ply_hair");
        playerQuery.addColumn("player", "ply_beard");
        playerQuery.addColumn("player", "ply_hairred");
        playerQuery.addColumn("player", "ply_hairgreen");
        playerQuery.addColumn("player", "ply_hairblue");
        playerQuery.addColumn("player", "ply_skinred");
        playerQuery.addColumn("player", "ply_skingreen");
        playerQuery.addColumn("player", "ply_skinblue");
        playerQuery.addEqualCondition("player", "ply_playerid", id);
        playerQuery.addServerTable("player");

        Database::Result playerResult = playerQuery.execute();

        if (playerResult.empty()) {
            throw LogoutException(NOACCOUNT);
        }

        Database::ResultTuple playerRow = playerResult.front();

        pos.x = playerRow["ply_posx"].as<int32_t>();
        pos.y = playerRow["ply_posy"].as<int32_t>();
        pos.z = playerRow["ply_posz"].as<int32_t>();
        faceto = (Character::face_to) playerRow["ply_faceto"].as<uint16_t>();

        setAttribute(Character::age, playerRow["ply_age"].as<uint16_t>());
        setAttribute(Character::weight, playerRow["ply_weight"].as<uint16_t>());
        setAttribute(Character::height, playerRow["ply_body_height"].as<uint16_t>());
        setAttribute(Character::hitpoints, playerRow["ply_hitpoints"].as<uint16_t>());
        setAttribute(Character::mana, playerRow["ply_mana"].as<uint16_t>());
        setAttribute(Character::attitude, playerRow["ply_attitude"].as<uint16_t>());
        setAttribute(Character::luck, playerRow["ply_luck"].as<uint16_t>());
        setAttribute(Character::strength, playerRow["ply_strength"].as<uint16_t>());
        setAttribute(Character::dexterity, playerRow["ply_dexterity"].as<uint16_t>());
        setAttribute(Character::constitution, playerRow["ply_constitution"].as<uint16_t>());
        setAttribute(Character::agility, playerRow["ply_agility"].as<uint16_t>());
        setAttribute(Character::intelligence, playerRow["ply_intelligence"].as<uint16_t>());
        setAttribute(Character::perception, playerRow["ply_perception"].as<uint16_t>());
        setAttribute(Character::willpower, playerRow["ply_willpower"].as<uint16_t>());
        setAttribute(Character::essence, playerRow["ply_essence"].as<uint16_t>());
        setAttribute(Character::foodlevel, playerRow["ply_foodlevel"].as<uint32_t>());

        SetAlive(playerRow["ply_lifestate"].as<uint16_t>() != 0);
        magic.type = (magic_type) playerRow["ply_magictype"].as<uint16_t>();
        magic.flags[MAGE] = playerRow["ply_magicflagsmage"].as<uint64_t>();
        magic.flags[PRIEST] = playerRow["ply_magicflagspriest"].as<uint64_t>();
        magic.flags[BARD] = playerRow["ply_magicflagsbard"].as<uint64_t>();
        magic.flags[DRUID] = playerRow["ply_magicflagsdruid"].as<uint64_t>();
        poisonvalue = playerRow["ply_poison"].as<uint16_t>();
        mental_capacity = playerRow["ply_mental_capacity"].as<uint32_t>();
        hair = playerRow["ply_hair"].as<uint16_t>();
        beard = playerRow["ply_beard"].as<uint16_t>();
        hairred = playerRow["ply_hairred"].as<uint16_t>();
        hairgreen = playerRow["ply_hairgreen"].as<uint16_t>();
        hairblue = playerRow["ply_hairblue"].as<uint16_t>();
        skinred = playerRow["ply_skinred"].as<uint16_t>();
        skingreen = playerRow["ply_skingreen"].as<uint16_t>();
        skinblue = playerRow["ply_skinblue"].as<uint16_t>();
    } catch (std::exception &e) {
        std::cerr << "exception on load player: " << e.what() << std::endl;
        throw LogoutException(NOCHARACTERFOUND);
    }
}

struct container_struct {
    Container *container;
    unsigned int id;
    unsigned int depotid;

    container_struct(Container *cc, unsigned int aboveid, unsigned int depot = 0)
        : container(cc), id(aboveid), depotid(depot) { }}
;

bool Player::save() throw() {
    using namespace Database;

    PConnection connection = ConnectionManager::getInstance().getConnection();

    try {
        connection->beginTransaction();

        {
            DeleteQuery introductionQuery(connection);
            introductionQuery.addEqualCondition<TYPE_OF_CHARACTER_ID>("introduction", "intro_player", id);
            introductionQuery.addServerTable("introduction");
            introductionQuery.execute();
        }

        {
            InsertQuery introductionQuery(connection);
            const InsertQuery::columnIndex playerColumn = introductionQuery.addColumn("intro_player");
            const InsertQuery::columnIndex knownPlayerColumn = introductionQuery.addColumn("intro_known_player");
            introductionQuery.addServerTable("introduction");

            for (auto it = knownPlayers.cbegin(); it != knownPlayers.cend(); ++it) {
                introductionQuery.addValue<TYPE_OF_CHARACTER_ID>(playerColumn, id);
                introductionQuery.addValue<TYPE_OF_CHARACTER_ID>(knownPlayerColumn, *it);
            }

            introductionQuery.execute();
        }

        time(&lastsavetime);
        {
            UpdateQuery query(connection);
            query.addAssignColumn<uint16_t>("chr_status", (uint16_t) status);
            query.addAssignColumn<std::string>("chr_lastip", last_ip);
            query.addAssignColumn<uint32_t>("chr_onlinetime", onlinetime + lastsavetime - logintime);
            query.addAssignColumn<std::string>("chr_prefix", prefix);
            query.addAssignColumn<std::string>("chr_suffix", suffix);
            query.addAssignColumn<time_t>("chr_lastsavetime", lastsavetime);

            if (status != 0) {
                query.addAssignColumn<time_t>("chr_statustime", statustime);
                query.addAssignColumn<TYPE_OF_CHARACTER_ID>("chr_statusgm", statusgm);
                query.addAssignColumn<std::string>("chr_statusreason", statusreason);
            } else {
                query.addAssignColumnNull("chr_statustime");
                query.addAssignColumnNull("chr_statusgm");
                query.addAssignColumnNull("chr_statusreason");
            }

            query.addEqualCondition<TYPE_OF_CHARACTER_ID>("chars", "chr_playerid", id);
            query.setServerTable("chars");

            query.execute();
        }

        {
            UpdateQuery query(connection);
            query.addAssignColumn<int32_t>("ply_posx", pos.x);
            query.addAssignColumn<int32_t>("ply_posy", pos.y);
            query.addAssignColumn<int32_t>("ply_posz", pos.z);
            query.addAssignColumn<uint16_t>("ply_faceto", (uint16_t) faceto);
            query.addAssignColumn<uint16_t>("ply_hitpoints", getAttribute(Character::hitpoints));
            query.addAssignColumn<uint16_t>("ply_mana", getAttribute(Character::mana));
            query.addAssignColumn<uint32_t>("ply_foodlevel", getAttribute(Character::foodlevel));
            query.addAssignColumn<uint32_t>("ply_lifestate", IsAlive() ? 1 : 0);
            query.addAssignColumn<uint32_t>("ply_magictype", (uint32_t) magic.type);
            query.addAssignColumn<uint64_t>("ply_magicflagsmage", magic.flags[MAGE]);
            query.addAssignColumn<uint64_t>("ply_magicflagspriest", magic.flags[PRIEST]);
            query.addAssignColumn<uint64_t>("ply_magicflagsbard", magic.flags[BARD]);
            query.addAssignColumn<uint64_t>("ply_magicflagsdruid", magic.flags[DRUID]);
            query.addAssignColumn<uint16_t>("ply_poison", poisonvalue);
            query.addAssignColumn<uint32_t>("ply_mental_capacity", mental_capacity);
            query.addAssignColumn<uint16_t>("ply_hair", hair);
            query.addAssignColumn<uint16_t>("ply_beard", beard);
            query.addAssignColumn<uint16_t>("ply_hairred", hairred);
            query.addAssignColumn<uint16_t>("ply_hairgreen", hairgreen);
            query.addAssignColumn<uint16_t>("ply_hairblue", hairblue);
            query.addAssignColumn<uint16_t>("ply_skinred", skinred);
            query.addAssignColumn<uint16_t>("ply_skingreen", skingreen);
            query.addAssignColumn<uint16_t>("ply_skinblue", skinblue);
            query.addEqualCondition<TYPE_OF_CHARACTER_ID>("player", "ply_playerid", id);
            query.addServerTable("player");
            query.execute();
        }

        {
            DeleteQuery query(connection);
            query.addEqualCondition<TYPE_OF_CHARACTER_ID>("playerskills", "psk_playerid", id);
            query.setServerTable("playerskills");
            query.execute();
        }

        if (!skills.empty()) {
            InsertQuery query(connection);
            const InsertQuery::columnIndex playerIdColumn = query.addColumn("psk_playerid");
            const InsertQuery::columnIndex skillIdColumn = query.addColumn("psk_skill_id");
            const InsertQuery::columnIndex valueColumn = query.addColumn("psk_value");
            const InsertQuery::columnIndex minorColumn = query.addColumn("psk_minor");

            // now store the skills
            for (SKILLMAP::iterator skillptr = skills.begin(); skillptr != skills.end(); ++skillptr) {
                query.addValue<uint16_t>(skillIdColumn, skillptr->first);
                query.addValue<uint16_t>(valueColumn, (uint16_t) skillptr->second.major);
                query.addValue<uint16_t>(minorColumn, (uint16_t) skillptr->second.minor);
            }

            query.addValues<TYPE_OF_CHARACTER_ID>(playerIdColumn, id, InsertQuery::FILL);
            query.addServerTable("playerskills");
            query.execute();
        }

        {
            DeleteQuery query(connection);
            query.addEqualCondition<TYPE_OF_CHARACTER_ID>("playeritems", "pit_playerid", id);
            query.setServerTable("playeritems");
            query.execute();
        }

        {
            DeleteQuery query(connection);
            query.addEqualCondition<TYPE_OF_CHARACTER_ID>("playeritem_datavalues", "idv_playerid", id);
            query.setServerTable("playeritem_datavalues");
            query.execute();
        }

        {
            std::list<container_struct> containers;

            // add backpack to containerlist
            if (characterItems[ BACKPACK ].getId() != 0 && backPackContents != NULL) {
                containers.push_back(container_struct(backPackContents, BACKPACK+1));
            }

            // add depot to containerlist
            std::map<uint32_t, Container *>::iterator it;

            for (it = depotContents.begin(); it != depotContents.end(); ++it) {
                containers.push_back(container_struct(it->second, 0, it->first));
            }

            int linenumber = 0;

            InsertQuery itemsQuery(connection);
            const InsertQuery::columnIndex itemsPlyIdColumn = itemsQuery.addColumn("pit_playerid");
            const InsertQuery::columnIndex itemsLineColumn = itemsQuery.addColumn("pit_linenumber");
            const InsertQuery::columnIndex itemsContainerColumn = itemsQuery.addColumn("pit_in_container");
            const InsertQuery::columnIndex itemsDepotColumn = itemsQuery.addColumn("pit_depot");
            const InsertQuery::columnIndex itemsItmIdColumn = itemsQuery.addColumn("pit_itemid");
            const InsertQuery::columnIndex itemsWearColumn = itemsQuery.addColumn("pit_wear");
            const InsertQuery::columnIndex itemsNumberColumn = itemsQuery.addColumn("pit_number");
            const InsertQuery::columnIndex itemsQualColumn = itemsQuery.addColumn("pit_quality");
            const InsertQuery::columnIndex itemsSlotColumn = itemsQuery.addColumn("pit_containerslot");
            itemsQuery.setServerTable("playeritems");

            InsertQuery dataQuery(connection);
            const InsertQuery::columnIndex dataPlyIdColumn = dataQuery.addColumn("idv_playerid");
            const InsertQuery::columnIndex dataLineColumn = dataQuery.addColumn("idv_linenumber");
            const InsertQuery::columnIndex dataKeyColumn = dataQuery.addColumn("idv_key");
            const InsertQuery::columnIndex dataValueColumn = dataQuery.addColumn("idv_value");
            dataQuery.setServerTable("playeritem_datavalues");

            // save all items directly on the body...
            for (int thisItemSlot = 0; thisItemSlot < MAX_BODY_ITEMS + MAX_BELT_SLOTS; ++thisItemSlot) {
                //if there is no item on this place, set all other values to 0
                if (characterItems[ thisItemSlot ].getId() == 0) {
                    characterItems[ thisItemSlot ].reset();
                }

                itemsQuery.addValue<int32_t>(itemsLineColumn, (int32_t)(++linenumber));
                itemsQuery.addValue<int16_t>(itemsContainerColumn, 0);
                itemsQuery.addValue<int32_t>(itemsDepotColumn, 0);
                itemsQuery.addValue<TYPE_OF_ITEM_ID>(itemsItmIdColumn, characterItems[thisItemSlot].getId());
                itemsQuery.addValue<uint16_t>(itemsWearColumn, characterItems[thisItemSlot].getWear());
                itemsQuery.addValue<uint16_t>(itemsNumberColumn, characterItems[thisItemSlot].getNumber());
                itemsQuery.addValue<uint16_t>(itemsQualColumn, characterItems[thisItemSlot].getQuality());
                itemsQuery.addValue<TYPE_OF_CONTAINERSLOTS>(itemsSlotColumn, 0);

                for (auto it = characterItems[ thisItemSlot ].getDataBegin(); it != characterItems[ thisItemSlot ].getDataEnd(); ++it) {
                    if (it->second.length() > 0) {
                        dataQuery.addValue<int32_t>(dataLineColumn, (int32_t) linenumber);
                        dataQuery.addValue<std::string>(dataKeyColumn, it->first);
                        dataQuery.addValue<std::string>(dataValueColumn, it->second);
                    }
                }
            }

            // add backpack contents...
            while (!containers.empty()) {
                // get container to save...
                container_struct &currentContainerStruct = containers.front();
                Container &currentContainer = *currentContainerStruct.container;
                auto containedItems = currentContainer.getItems();

                for (auto it = containedItems.cbegin(); it != containedItems.cend(); ++it) {
                    const Item &item = it->second;
                    itemsQuery.addValue<int32_t>(itemsLineColumn, (int32_t)(++linenumber));
                    itemsQuery.addValue<int16_t>(itemsContainerColumn, (int16_t) currentContainerStruct.id);
                    itemsQuery.addValue<int32_t>(itemsDepotColumn, (int32_t) currentContainerStruct.depotid);
                    itemsQuery.addValue<TYPE_OF_ITEM_ID>(itemsItmIdColumn, item.getId());
                    itemsQuery.addValue<uint16_t>(itemsWearColumn, item.getWear());
                    itemsQuery.addValue<uint16_t>(itemsNumberColumn, item.getNumber());
                    itemsQuery.addValue<uint16_t>(itemsQualColumn, item.getQuality());
                    itemsQuery.addValue<TYPE_OF_CONTAINERSLOTS>(itemsSlotColumn, it->first);

                    for (auto it2 = item.getDataBegin(); it2 != item.getDataEnd(); ++it2) {
                        dataQuery.addValue<int32_t>(dataLineColumn, (int32_t) linenumber);
                        dataQuery.addValue<std::string>(dataKeyColumn, it2->first);
                        dataQuery.addValue<std::string>(dataValueColumn, it2->second);
                    }

                    // if it is a container, add it to the list of containers to save...
                    if (item.isContainer()) {
                        auto containedContainers = currentContainer.getContainers();
                        auto iterat = containedContainers.find(it->first);

                        if (iterat != containedContainers.end()) {
                            containers.push_back(container_struct(iterat->second, linenumber));
                        }
                    }
                }

                containers.pop_front();
            }

            itemsQuery.addValues(itemsPlyIdColumn, id, InsertQuery::FILL);
            dataQuery.addValues(dataPlyIdColumn, id, InsertQuery::FILL);

            itemsQuery.execute();
            dataQuery.execute();
        }

        connection->commitTransaction();

        if (!effects->save()) {
            std::cerr<<"error while saving lteffects for player"<<name<<std::endl;
        }

        return true;
    } catch (std::exception &e) {
        std::cerr << "Playersave caught exception: " << e.what() << std::endl;
        connection->rollbackTransaction();
        return false;
    }
}

bool Player::loadGMFlags() throw() {
    try {
        using namespace Database;
        SelectQuery query;
        query.addColumn("gms", "gm_rights_server");
        query.addEqualCondition<TYPE_OF_CHARACTER_ID>("gms", "gm_charid", id);
        query.addServerTable("gms");

        Result results = query.execute();

        if (results.empty()) {
            setAdmin(0);
        } else {
            setAdmin(results.front()["gm_rights_server"].as<uint32_t>());
        }

        return true;
    } catch (std::exception &e) {
        std::cerr<<"exception: "<<e.what()<<" while loading GM Flags!"<<std::endl;
        return false;
    }

    return false;
}

bool Player::load() throw() {
    std::map<int, Container *> depots, containers;
    std::map<int, Container *>::iterator it;

    bool dataOK=true;

    using namespace Database;
    PConnection connection = ConnectionManager::getInstance().getConnection();

    try {

        {
            SelectQuery query;
            query.addColumn("introduction", "intro_known_player");
            query.addEqualCondition<TYPE_OF_CHARACTER_ID>("introduction", "intro_player", id);
            query.addServerTable("introduction");

            Result results = query.execute();

            for (ResultConstIterator it = results.begin(); it != results.end(); ++it) {
                knownPlayers.insert((*it)["intro_known_player"].as<TYPE_OF_CHARACTER_ID>());
            }
        }

        {
            SelectQuery query;
            query.addColumn("playerskills", "psk_skill_id");
            query.addColumn("playerskills", "psk_value");
            query.addColumn("playerskills", "psk_minor");
            query.addEqualCondition<TYPE_OF_CHARACTER_ID>("playerskills", "psk_playerid", id);
            query.addServerTable("playerskills");

            Result results = query.execute();

            if (!results.empty()) {
                for (ResultConstIterator itr = results.begin();
                     itr != results.end(); ++itr) {
                    setSkill(
                        (*itr)["psk_skill_id"].as<uint16_t>(),
                        (*itr)["psk_value"].as<uint16_t>(),
                        (*itr)["psk_minor"].as<uint16_t>()
                    );
                }
            } else {
                std::cout<<"No Skills to load for "<<name<<"("<<id<<")"<<std::endl;
            }
        }

        // load data values
        std::vector<uint16_t> ditemlinenumber;
        std::vector<std::string> key;
        std::vector<std::string> value;
        {
            SelectQuery query;
            query.addColumn("playeritem_datavalues", "idv_linenumber");
            query.addColumn("playeritem_datavalues", "idv_key");
            query.addColumn("playeritem_datavalues", "idv_value");
            query.addEqualCondition<TYPE_OF_CHARACTER_ID>("playeritem_datavalues", "idv_playerid", id);
            query.addOrderBy("playeritem_datavalues", "idv_linenumber", SelectQuery::ASC);
            query.addServerTable("playeritem_datavalues");

            Result results = query.execute();

            for (ResultConstIterator itr = results.begin(); itr != results.end(); ++itr) {
                ditemlinenumber.push_back((*itr)["idv_linenumber"].as<uint16_t>());
                key.push_back((*itr)["idv_key"].as<std::string>());
                value.push_back((*itr)["idv_value"].as<std::string>());
            }
        }
        size_t dataRows = ditemlinenumber.size();

        // load inventory
        std::vector<uint16_t> itemlinenumber;
        std::vector<uint16_t> itemincontainer;
        std::vector<uint32_t> itemdepot;
        std::vector<Item::id_type> itemid;
        std::vector<Item::wear_type> itemwear;
        std::vector<Item::number_type> itemnumber;
        std::vector<Item::quality_type> itemquality;
        std::vector<TYPE_OF_CONTAINERSLOTS> itemcontainerslot;
        {
            SelectQuery query;
            query.addColumn("playeritems", "pit_linenumber");
            query.addColumn("playeritems", "pit_in_container");
            query.addColumn("playeritems", "pit_depot");
            query.addColumn("playeritems", "pit_itemid");
            query.addColumn("playeritems", "pit_wear");
            query.addColumn("playeritems", "pit_number");
            query.addColumn("playeritems", "pit_quality");
            query.addColumn("playeritems", "pit_containerslot");
            query.addEqualCondition<TYPE_OF_CHARACTER_ID>("playeritems", "pit_playerid", id);
            query.addOrderBy("playeritems", "pit_linenumber", SelectQuery::ASC);
            query.addServerTable("playeritems");

            Result results = query.execute();

            for (ResultConstIterator itr = results.begin(); itr != results.end(); ++itr) {
                itemlinenumber.push_back((*itr)["pit_linenumber"].as<uint16_t>());
                itemincontainer.push_back((*itr)["pit_in_container"].as<uint16_t>());
                itemdepot.push_back((*itr)["pit_depot"].as<uint32_t>());
                itemid.push_back((*itr)["pit_itemid"].as<Item::id_type>());
                itemwear.push_back((Item::wear_type)((*itr)["pit_wear"].as<uint16_t>()));
                itemnumber.push_back((*itr)["pit_number"].as<Item::number_type>());
                itemquality.push_back((*itr)["pit_quality"].as<Item::quality_type>());
                itemcontainerslot.push_back((*itr)["pit_containerslot"].as<TYPE_OF_CONTAINERSLOTS>());
            }
        }
        size_t itemRows = itemlinenumber.size();

        // load depots
        std::vector<uint32_t> depotid;
        {
            SelectQuery query;
            query.setDistinct(true);
            query.addColumn("playeritems", "pit_depot");
            query.addEqualCondition<TYPE_OF_CHARACTER_ID>("playeritems", "pit_playerid", id);
            query.addServerTable("playeritems");

            Result results = query.execute();

            for (ResultConstIterator itr = results.begin(); itr != results.end(); ++itr) {
                depotid.push_back((*itr)["pit_depot"].as<uint32_t>());
            }
        }

        size_t depotRows = depotid.size();

        for (size_t i = 0; i < depotRows; ++i) {
            if (depotid[i] != 0) {
                depotContents[depotid[i]] = new Container(DEPOTITEM);
                depots[depotid[i]] = depotContents[depotid[i]];
            }
        }

        unsigned int tempdepot, tempincont, linenumber;
        Container *tempc;
        unsigned int curdatalinenumber = 0;

        for (unsigned int tuple = 0; tuple < itemRows; ++tuple) {
            tempincont = itemincontainer[tuple];
            tempdepot = itemdepot[tuple];
            linenumber = itemlinenumber[tuple];

            Item tempi(itemid[tuple],
                       itemnumber[tuple],
                       itemwear[tuple],
                       itemquality[tuple]
                      );

            while (curdatalinenumber < dataRows && ditemlinenumber[curdatalinenumber] == linenumber) {
                tempi.setData(key[curdatalinenumber], value[curdatalinenumber]);
                curdatalinenumber++;
            }

            // item is in a depot?
            if (tempdepot && (it = depots.find(tempdepot)) == depots.end()) {
                // serious error occured! player data corrupted!
                std::cerr << "*** player '" << name << "' has invalid depot contents!" << std::endl;
                Logger::writeError("itemload","*** player '" + name + "' has invalid depot contents!");
                throw std::exception();
            }

            // item is in a container?
            if (dataOK && tempincont && (it = containers.find(tempincont)) == containers.end()) {
                // serious error occured! player data corrupted!
                std::cerr << "*** player '" << name << "' has invalid container contents!" << std::endl;
                Logger::writeError("itemload","*** player '" + name + "' has invalid depot contents 2!");
                throw std::exception();
            }

            if ((dataOK && ((!tempincont && ! tempdepot) && linenumber > MAX_BODY_ITEMS + MAX_BELT_SLOTS)) || (tempincont && tempdepot)) {
                // serious error occured! player data corrupted!
                std::cerr << "*** player '" << name << "' has invalid items!" << std::endl;
                Logger::writeError("itemload","*** player '" + name + "' has invalid items!");
                throw std::exception();
            }

            if (tempi.isContainer()) {
                tempc = new Container(tempi.getId());

                if (linenumber > MAX_BODY_ITEMS + MAX_BELT_SLOTS) {
                    if (!it->second->InsertContainer(tempi, tempc, itemcontainerslot[tuple])) {
                        Logger::writeError("itemload","*** player '" + name + "' insert Container wasn't sucessful!");
                    } else {

                    }
                } else {
                    characterItems[ linenumber - 1 ] = tempi;
                }

                containers[linenumber] = tempc;
            } else {
                if (linenumber >= MAX_BODY_ITEMS + MAX_BELT_SLOTS + 1) {
                    it->second->InsertItem(tempi, itemcontainerslot[tuple]);
                } else {
                    characterItems[ linenumber - 1 ] = tempi;
                }
            }
        }

        if ((it=containers.find(BACKPACK + 1)) != containers.end()) {
            backPackContents = it->second;
        } else {
            backPackContents = NULL;
        }

        std::map<uint32_t,Container *>::iterator it2;

        for (it = depots.begin(); it != depots.end(); ++it) {
            if ((it2=depotContents.find(it->first)) != depotContents.end()) {
                it2->second = it->second;
            } else {
                it2->second = NULL;
            }
        }
    } catch (std::exception &e) {
        std::cerr << "exception: " << e.what() << std::endl;
        dataOK = false;
    }

    if (!dataOK) {
        std::cerr<<"in load(): error while loading!"<<std::endl;
        //Fehler beim laden aufgetreten
        std::map<int, Container *>::reverse_iterator rit;
        std::map<uint32_t,Container *>::reverse_iterator rit2;

        // clean up...
        for (rit=containers.rbegin(); rit != containers.rend(); ++rit) {
            delete rit->second;
        }

        for (rit=depots.rbegin(); rit != depots.rend(); ++rit) {
            delete rit->second;
        }

        for (rit2 = depotContents.rbegin(); rit2 != depotContents.rend(); ++rit2) {
            delete rit2->second;
        }

        backPackContents=NULL;
    }

    //#endif
    return dataOK;
}

void Player::increasePoisonValue(short int value) {
    if ((poisonvalue == 0) && value > 0) {
        std::string german = "Du bist vergiftet.";
        std::string english = "You are poisoned.";
        informLua(german, english);
    }

    if ((poisonvalue + value) >= MAXPOISONVALUE) {
        poisonvalue = MAXPOISONVALUE;
    } else if ((poisonvalue + value) <= 0) {
        poisonvalue = 0;
        std::string german = "Es geht dir auf einmal besser.";
        std::string english = "You suddenly feel better.";
        informLua(german, english);
    } else {
        poisonvalue += value;
    }


    //============================================================================
}

unsigned short int Player::setSkill(TYPE_OF_SKILL_ID skill, short int major, short int minor) {
    Character::setSkill(skill, major, minor);
    sendSkill(skill, major, minor);
    return major;
}

unsigned short int Player::increaseSkill(TYPE_OF_SKILL_ID skill, short int amount) {
    Character::increaseSkill(skill, amount);
    int major = getSkill(skill);
    int minor = getMinorSkill(skill);
    sendSkill(skill, major, minor);
    return major;
}

void Player::receiveText(talk_type tt, std::string message, Character *cc) {
    boost::shared_ptr<BasicServerCommand>cmd(new SayTC(cc->pos.x, cc->pos.y, cc->pos.z, message));

    switch (tt) {
    case tt_say:
        Connection->addCommand(cmd);
        break;
    case tt_whisper:
        cmd.reset(new WhisperTC(cc->pos.x, cc->pos.y, cc->pos.z, message));
        Connection->addCommand(cmd);
        break;
    case tt_yell:
        cmd.reset(new ShoutTC(cc->pos.x, cc->pos.y, cc->pos.z, message));
        Connection->addCommand(cmd);
        break;
    }
}

bool Player::knows(Player *player) const {
    return this == player || knownPlayers.find(player->id) != knownPlayers.cend();
}

void Player::getToKnow(Player *player) {
    if (!knows(player)) {
        knownPlayers.insert(player->id);
    }
}

void Player::introducePlayer(Player *player) {
    getToKnow(player);

    boost::shared_ptr<BasicServerCommand>cmd(new IntroduceTC(player->id, player->name));
    Connection->addCommand(cmd);
}

void Player::deleteAllSkills() {
    Character::deleteAllSkills();
    sendAllSkills();

}

void Player::teachMagic(unsigned char type,unsigned char flag) {
    //>0 braucht nicht abgefragt werden da unsinged char immer >=0 sein muss
    if ((type < 4)) {
        unsigned long int tflags=0;

        for (int i = 0; i < 4; ++i) {
            //  std::cout << magic.flags[ i ] << " ";
            tflags|=magic.flags[ i ];
        }

        // Prfen ob der Char schon flags hat. Wenn neun dann neue Magierichtung.
        if (tflags == 0) {
            //   std::cout << "tflags==0, PARAMTYPE type= " << type << std::endl;
            switch (type) {
            case 0:
                magic.type = MAGE;
                break;
            case 1:
                magic.type = PRIEST;
                break;
            case 2:
                magic.type = BARD;
                break;
            case 3:
                magic.type = DRUID;
                break;
            default:
                magic.type = MAGE;
                break;
            }
        }

        unsigned long int temp = 1;
        temp <<= flag;
        //std::cout << "addiere flag " << temp << std::endl;
        magic.flags[ type ] |= temp;
        sendMagicFlags(magic.type);
    }
}

void Player::inform(std::string message, informType type) {
    boost::shared_ptr<BasicServerCommand>cmd(new InformTC(type, message));
    Connection->addCommand(cmd);
}

void Player::informLua(std::string message) {
    inform(message, informScriptMediumPriority);
}

void Player::informLua(std::string german, std::string english) {
    informLua(nls(german, english));
}

void Player::informLua(std::string message, informType type) {
    switch (type) {
    case informScriptLowPriority:
    case informScriptMediumPriority:
    case informScriptHighPriority:
        inform(message, type);
        break;
    default:
        informLua(message);
        break;
    }
}

void Player::informLua(std::string german, std::string english, informType type) {
    informLua(nls(german, english), type);
}

bool Player::encumberance(uint16_t &movementCost) {
    int perEncumb = (LoadWeight() * 100) / maxLoadWeight();

    if (perEncumb > 75) {
        if (!wasEncumberedSent()) {
            setEncumberedSent(true);
            std::string german = "Deine Last bremst dich.";
            std::string english = "Your burden slows you down.";
            informLua(nls(german, english));
        }

        if (perEncumb > 100) {
            std::string german = "Deine Last hindert dich daran zu laufen.";
            std::string english = "Your burden keeps you from moving.";
            informLua(nls(german, english));
            return false;
        }

        setEncumberedSent(false);

        movementCost += movementCost * 3 * (perEncumb-75) / 25;
    }

    return true;
}

bool Player::move(direction dir, uint8_t mode) {
#ifdef PLAYER_MOVE_DEBUG
    std::cout<<"Player::move position old: ";
    std::cout << pos.x << " " << pos.y << " " << pos.z << std::endl;
#endif
    //Ggf Scriptausfhrung wenn der Spieler auf ein Triggerfeld tritt
    _world->TriggerFieldMove(this,false);
    closeOnMove();

    // if we actively move we look into that direction...
    if (mode != PUSH && dir != dir_up && dir != dir_down) {
        faceto = (Character::face_to)dir;
    }

    size_t steps;
    size_t j = 0;
    bool cont = true;

    if (mode != RUNNING) {
        steps = 1;
    } else {
        steps = 2;
    }

    position newpos, oldpos;
    uint8_t waitpages = 0;

    while (j < steps && cont) {

        // check if we can move to our target field
        newpos = pos;
        oldpos = pos;

        newpos.x += _world->moveSteps[ dir ][ 0 ];
        newpos.y += _world->moveSteps[ dir ][ 1 ];
        newpos.z += _world->moveSteps[ dir ][ 2 ];

        Field *cfold=NULL;
        Field *cfnew=NULL;

        bool fieldfound = false;

        // get the old tile... we need it to update the old tile as well as for the walking cost
        if (!_world->GetPToCFieldAt(cfold, pos.x, pos.y, pos.z)) {
            return false;
        }

        // we need to search for tiles below this level
        for (size_t i = 0; i < RANGEDOWN + 1 && !fieldfound; ++i) {
            fieldfound = _world->GetPToCFieldAt(cfnew, newpos.x, newpos.y, newpos.z, _world->tmap);

            // did we hit a targetfield?
            if (!fieldfound || cfnew->getTileId() == TRANSPARENTDISAPPEAR || cfnew->getTileId() == TRANSPARENT) {
                fieldfound = false;
                --newpos.z;
            }
        }

        if (cfnew != NULL && fieldfound && (cfnew->moveToPossible() || (getClippingActive() == false && (isAdmin() || hasGMRight(gmr_isnotshownasgm))))) {
            uint16_t walkcost = getMovementCost(cfnew);
#ifdef PLAYER_MOVE_DEBUG
            std::cout<< "Player::move Walkcost beforce encumberance: " << walkcost << std::endl;
#endif

            if (!encumberance(walkcost)) {
#ifdef PLAYER_MOVE_DEBUG
                std::cout<< "Player::move Walkcost after encumberance Char overloadet: " << walkcost << std::endl;
#endif
                //Char ueberladen
                boost::shared_ptr<BasicServerCommand>cmd(new MoveAckTC(id, pos, NOMOVE, 0));
                Connection->addCommand(cmd);
                return false;
            } else {
#ifdef PLAYER_MOVE_DEBUG
                std::cout<< "Player::move Walkcost after Char not overloadet encumberance: " << walkcost << std::endl;
#endif
                int16_t diff = (P_MIN_AP - actionPoints + walkcost) * 10;

                // necessary to get smooth movement in client (dunno how this one is supposed to work exactly)
                if (diff < 60) {
                    waitpages = 4;
                } else {
                    waitpages = (diff * 667) / 10000;
                }

                if (mode != RUNNING || (j == 1 && cont)) {
                    actionPoints -= walkcost;
                }
            }

#ifdef PLAYER_MOVE_DEBUG
            std::cout << "Player::move : Bewegung moeglich" << std::endl;
#endif
            // Spieler vom alten Feld nehmen
            cfold->removeChar();

            // Spieler auf das neue Feld setzen
            cfnew->setChar();

            if (newpos.z != oldpos.z) {
                //Z Coordinate hat sich ge�ndert komplettes update senden (Sp�ter durch teilupdate ersetzen)
                updatePos(newpos);
                boost::shared_ptr<BasicServerCommand>cmd(new MoveAckTC(id, pos, NOMOVE, 0));
                Connection->addCommand(cmd);
                // Koordinate
                cmd.reset(new SetCoordinateTC(pos));
                Connection->addCommand(cmd);
                sendFullMap();
                cont = false;
            } else {
                if (mode != RUNNING || (j == 1 && cont)) {
                    boost::shared_ptr<BasicServerCommand> cmd(new MoveAckTC(id, newpos, mode, waitpages));
                    Connection->addCommand(cmd);
                }

                if (j == 1 && cont) {
                    sendStepStripes(dir);
                }

                pos.x = newpos.x;
                pos.y = newpos.y;

                if (mode != RUNNING || (j == 1 && cont)) {
                    sendStepStripes(dir);
                }
            }

            // allen anderen Spielern die Bewegung bermitteln
            if (mode != RUNNING || j == 1 || !cont) {
                _world->sendCharacterMoveToAllVisiblePlayers(this, mode, waitpages);

#ifdef PLAYER_MOVE_DEBUG
                std::cout << "Player move position new: ";
                std::cout << pos.x << " " << pos.y << " " << pos.z << std::endl;
#endif

                _world->sendAllVisibleCharactersToPlayer(this, true);
            }

            //Prfen ob Zielfeld ein Teleporter und evtl Spieler teleportieren
            if (cfnew->IsWarpField()) {
                position newpos;
                cfnew->GetWarpField(newpos);
                Warp(newpos);
                cont = false;
            }

            //Prfen ob das Feld ein spezielles feld ist.
            _world->checkFieldAfterMove(this, cfnew);

            //Ggf Scriptausfhrung beim Betreten eines Triggerfeldes
            _world->TriggerFieldMove(this,true);
            //send the move to the monitoring clients
            boost::shared_ptr<BasicServerCommand>cmd(new BBPlayerMoveTC(id, pos.x, pos.y, pos.z));
            _world->monitoringClientList->sendCommand(cmd);

            if (mode != RUNNING || j == 1) {
                return true;
            }
        } // neues Feld vorhanden und passierbar?
        else {
            if (j == 1) {
                boost::shared_ptr<BasicServerCommand> cmd(new MoveAckTC(id, pos, NORMALMOVE, waitpages));
                Connection->addCommand(cmd);
                sendStepStripes(dir);
                _world->sendCharacterMoveToAllVisiblePlayers(this, mode, waitpages);
                _world->sendAllVisibleCharactersToPlayer(this, true);
                return true;
            } else if (j == 0) {
                boost::shared_ptr<BasicServerCommand>cmd(new MoveAckTC(id, pos, NOMOVE, 0));
                Connection->addCommand(cmd);
                return false;
            }
        }

        ++j;
    } // loop (steps)

#ifdef PLAYER_MOVE_DEBUG
    std::cout << "movePlayer: Bewegung nicht moeglich \n";
#endif
    boost::shared_ptr<BasicServerCommand>cmd(new MoveAckTC(id, pos, NOMOVE, 0));
    Connection->addCommand(cmd);
    return false;
}


bool Player::Warp(position newPos) {
    bool warped = Character::Warp(newPos);

    if (warped) {
        handleWarp();
        return true;
    }

    return false;
}

bool Player::forceWarp(position newPos) {
    bool warped = Character::forceWarp(newPos);

    if (warped) {
        handleWarp();
        return true;
    }

    return false;
}

void Player::handleWarp() {
    closeOnMove();
    boost::shared_ptr<BasicServerCommand>cmd(new SetCoordinateTC(pos));
    Connection->addCommand(cmd);
    sendFullMap();
    visibleChars.clear();
    _world->sendAllVisibleCharactersToPlayer(this, true);
    cmd.reset(new BBPlayerMoveTC(id, pos.x, pos.y, pos.z));
    _world->monitoringClientList->sendCommand(cmd);
}

void Player::openDepot(uint16_t depotid) {
    //_world->lookIntoDepot(this, 0 );
#ifdef PLAYER_PlayerDepot_DEBUG
    std::cout << "lookIntoDepot: Spieler " << cp->name << " schaut in sein Depot" << std::endl;
#endif

    //std::map<uint32_t,Container*>::iterator it;
    if (depotContents.find(depotid) != depotContents.end()) {
        if (depotContents[depotid] != NULL) {
#ifdef PLAYER_PlayerDepot_DEBUG
            std::cout << "Depotinhalt vorhanden" << std::endl;
#endif
            openShowcase(depotContents[depotid], false);
#ifdef PLAYER_PlayerDepot_DEBUG
            std::cout << "lookIntoDepot: Ende" << std::endl;
#endif
        }
    } else {
#ifdef PLAYER_PlayerDepot_DEBUG
        std::cout << "Depot mit der ID: " << depotid << " wird neu erstellt!" << std::endl;
#endif
        depotContents[depotid] = new Container(DEPOTITEM);
        openShowcase(depotContents[depotid], false);
    }

#ifdef PLAYER_PlayerDepot_DEBUG
    std::cout << "lookIntoDepot: Ende" << std::endl;
#endif
}

void Player::changeQualityItem(TYPE_OF_ITEM_ID id, short int amount) {
    Character::changeQualityItem(id, amount);
    updateBackPackView();

    for (unsigned char i = MAX_BELT_SLOTS + MAX_BODY_ITEMS - 1; i > 0; --i) {
        //Update des Inventorys.
        sendCharacterItemAtPos(i);
    }
}

void Player::changeQualityAt(unsigned char pos, short int amount) {
    //std::cout<<"in Player changeQualityAt!"<<std::endl;
    Character::changeQualityAt(pos, amount);
    //std::cout<<"ende des ccharacter ChangeQualityAt aufrufes"<<std::endl;
    sendCharacterItemAtPos(pos);
    sendCharacterItemAtPos(LEFT_TOOL);   //Item in der Linken hand nochmals senden um ggf ein gel�chtes Belegt an zu zeigen.
    sendCharacterItemAtPos(RIGHT_TOOL);   //Item in der rechten Hand nochmals senden um ggf ein gel�chtes Belegt an zu zeigen.
}

bool Player::hasGMRight(gm_rights right) {
    return ((right & admin) == static_cast<uint32_t>(right));
}

void Player::setQuestProgress(uint16_t questid, uint32_t progress) throw() {
    using namespace Database;
    PConnection connection = ConnectionManager::getInstance().getConnection();

    try {
        connection->beginTransaction();

        SelectQuery query(connection);
        query.addColumn("questprogress", "qpg_progress");
        query.addEqualCondition<TYPE_OF_CHARACTER_ID>("questprogress", "qpg_userid", id);
        query.addEqualCondition<uint16_t>("questprogress", "qpg_questid", questid);
        query.addServerTable("questprogress");

        Result results = query.execute();

        save();

        if (results.empty()) {
            InsertQuery insQuery;
            const InsertQuery::columnIndex userColumn = insQuery.addColumn("qpg_userid");
            const InsertQuery::columnIndex questColumn = insQuery.addColumn("qpg_questid");
            const InsertQuery::columnIndex progressColumn = insQuery.addColumn("qpg_progress");
            insQuery.addServerTable("questprogress");

            insQuery.addValue<TYPE_OF_CHARACTER_ID>(userColumn, id);
            insQuery.addValue<uint16_t>(questColumn, questid);
            insQuery.addValue<uint32_t>(progressColumn, progress);

            insQuery.execute();
        } else {
            UpdateQuery updQuery;
            updQuery.addAssignColumn<uint32_t>("qpg_progress", progress);
            updQuery.addEqualCondition<TYPE_OF_CHARACTER_ID>("questprogress", "qpg_userid", id);
            updQuery.addEqualCondition<uint16_t>("questprogress", "qpg_questid", questid);
            updQuery.setServerTable("questprogress");

            updQuery.execute();
        }

        connection->commitTransaction();
    } catch (std::exception &e) {
        std::cerr<<"exception: "<<e.what()<<" while setting QuestProgress!"<<std::endl;
        connection->rollbackTransaction();
    }
}

uint32_t Player::getQuestProgress(uint16_t questid) throw() {
    try {
        using namespace Database;

        SelectQuery query;
        query.addColumn("questprogress", "qpg_progress");
        query.addEqualCondition<TYPE_OF_CHARACTER_ID>("questprogress", "qpg_userid", id);
        query.addEqualCondition<uint16_t>("questprogress", "qpg_questid", questid);
        query.addServerTable("questprogress");

        Result results = query.execute();

        if (results.empty()) {
            return UINT32_C(0);
        } else {
            return results.front()["qpg_progress"].as<uint32_t>();
        }
    } catch (std::exception &e) {
        std::cerr<<"exception: "<<e.what()<<" while getting QuestProgress!"<<std::endl;
        return UINT32_C(0);
    }

    return UINT32_C(0);
}

void Player::startAction(unsigned short int wait, unsigned short int ani, unsigned short int redoani, unsigned short int sound, unsigned short int redosound) {
    ltAction->startLongTimeAction(wait, ani, redoani, sound, redosound);
}

void Player::abortAction() {
    ltAction->abortAction();
}

void Player::successAction() {
    ltAction->successAction();
}

void Player::actionDisturbed(Character *disturber) {
    ltAction->actionDisturbed(disturber);
}

void Player::changeSource(Character *cc) {
    ltAction->changeSource(cc);
}

void Player::changeSource(ScriptItem sI) {
    ltAction->changeSource(sI);
}

void Player::changeSource(position pos) {
    ltAction->changeSource(pos);
}

void Player::changeSource() {
    ltAction->changeSource();
}

void Player::changeTarget(Character *cc) {
    ltAction->changeTarget(cc);
}

void Player::changeTarget(ScriptItem sI) {
    ltAction->changeTarget(sI);
}

void Player::changeTarget(position pos) {
    ltAction->changeTarget(pos);
}

void Player::changeTarget() {
    ltAction->changeTarget();
}

std::string Player::getSkillName(TYPE_OF_SKILL_ID s) {
    SkillStruct skillStruct;

    if (Skills->find(s, skillStruct)) {
        return nls(skillStruct.germanName, skillStruct.englishName);
    } else {
        std::string german("unbekannter Skill");
        std::string english("unknown skill");
        return nls(german, english);
    }
}

const unsigned short int Player::getPlayerLanguage() {
    return _player_language->_language;
}

void Player::setPlayerLanguage(Language::LanguageType mother_tongue) {
    _player_language = Language::create(mother_tongue);
}

void Player::sendRelativeArea(int8_t zoffs) {
    if ((screenwidth == 0) && (screenheight == 0)) {
        // static view
        int x = pos.x;
        int y = pos.y - MAP_DIMENSION;
        int z = pos.z + zoffs;
        int e = zoffs * 3;

        if (zoffs < 0) {
            x -= e;
            y += e;
            e = 0;
        }

        //schleife von 0ben nach unten durch alle tiles
        World *world = World::get();

        for (int i=0; i <= (MAP_DIMENSION + MAP_DOWN_EXTRA + e) * 2; ++i) {
            world->clientview.fillStripe(position(x,y,z), NewClientView::dir_right, MAP_DIMENSION+1-(i%2), World::get()->maps);

            if (world->clientview.getExists()) {
                Connection->addCommand(boost::shared_ptr<BasicServerCommand>(new MapStripeTC(position(x,y,z), NewClientView::dir_right)));
            }

            if (i % 2 == 0) {
                y += 1;
            } else {
                x -= 1;
            }
        }
    } else {
        // dynamic view
        int x = pos.x - screenwidth + screenheight;
        int y = pos.y - screenwidth - screenheight;
        int z = pos.z + zoffs;
        int e = zoffs * 3;

        if (zoffs < 0) {
            x -= e;
            y += e;
            e = 0;
        }

        //schleife von 0ben nach unten durch alle tiles
        World *world = World::get();

        for (int i=0; i <= (2*screenheight + MAP_DOWN_EXTRA + e) * 2; ++i) {
            world->clientview.fillStripe(position(x,y,z), NewClientView::dir_right, 2*screenwidth+1-(i%2), World::get()->maps);

            if (world->clientview.getExists()) {
                Connection->addCommand(boost::shared_ptr<BasicServerCommand>(new MapStripeTC(position(x,y,z), NewClientView::dir_right)));
            }

            if (i % 2 == 0) {
                y += 1;
            } else {
                x -= 1;
            }
        }
    }
}

void Player::sendFullMap() {
    for (int8_t i = -2; i <= 2; ++i) {
        sendRelativeArea(i);
    }

    Connection->addCommand(boost::shared_ptr<BasicServerCommand>(new MapCompleteTC()));
}

void Player::sendDirStripe(viewdir direction, bool extraStripeForDiagonalMove) {
    if ((screenwidth == 0) && (screenheight == 0)) {
        // static view
        int x = {},y = {},z,e,l;
        NewClientView::stripedirection dir = {};
        int length = MAP_DIMENSION + 1;

        switch (direction) {
        case upper:
            x = pos.x;
            y = pos.y - MAP_DIMENSION;
            dir = NewClientView::dir_right;

            if (extraStripeForDiagonalMove) {
                --x;
            }

            break;
        case left:
            x = pos.x;
            y = pos.y - MAP_DIMENSION;
            dir = NewClientView::dir_down;
            length += MAP_DOWN_EXTRA;

            if (extraStripeForDiagonalMove) {
                ++x;
            }

            break;
        case right:
            x = pos.x + MAP_DIMENSION;
            y = pos.y;
            dir = NewClientView::dir_down;
            length += MAP_DOWN_EXTRA;

            if (extraStripeForDiagonalMove) {
                --y;
            }

            break;
        case lower:
            x = pos.x - MAP_DIMENSION - MAP_DOWN_EXTRA;
            y = pos.y + MAP_DOWN_EXTRA;
            dir = NewClientView::dir_right;

            if (extraStripeForDiagonalMove) {
                --y;
            }

            break;
        }

        NewClientView *view = &(World::get()->clientview);

        for (z = - 2; z <= 2; ++z) {
            e = (direction != lower && z > 0) ? z*3 : 0; // left, right and upper stripes moved up if z>0 to provide the client with info for detecting roofs
            l = (dir == NewClientView::dir_down && z > 0) ? e : 0; // right and left stripes have to become longer then

            if (extraStripeForDiagonalMove) {
                ++l;
            }

            view->fillStripe(position(x-z*3+e,y+z*3-e,pos.z+z), dir, length+l, World::get()->maps);

            if (view->getExists()) {
                Connection->addCommand(boost::shared_ptr<BasicServerCommand>(new MapStripeTC(position(x-z*3+e,y+z*3-e,pos.z+z), dir)));
            }
        }
    } else {
        // dynamic view
        int x = {},y = {},z,e,l;
        NewClientView::stripedirection dir = {};
        int length = {};

        switch (direction) {
        case upper:
            x = pos.x - screenwidth + screenheight;
            y = pos.y - screenwidth - screenheight;
            dir = NewClientView::dir_right;
            length = 2*screenwidth + 1;

            if (extraStripeForDiagonalMove) {
                --x;
            }

            break;
        case left:
            x = pos.x - screenwidth + screenheight;
            y = pos.y - screenwidth - screenheight;
            dir = NewClientView::dir_down;
            length = 2*screenheight + 1 + MAP_DOWN_EXTRA;

            if (extraStripeForDiagonalMove) {
                ++x;
            }

            break;
        case right:
            x = pos.x + screenwidth + screenheight;
            y = pos.y + screenwidth - screenheight;
            dir = NewClientView::dir_down;
            length = 2*screenheight + 1 + MAP_DOWN_EXTRA;

            if (extraStripeForDiagonalMove) {
                --y;
            }

            break;
        case lower:
            x = pos.x - screenwidth - screenheight - MAP_DOWN_EXTRA;
            y = pos.y - screenwidth + screenheight + MAP_DOWN_EXTRA;
            dir = NewClientView::dir_right;
            length = 2*screenwidth + 1;

            if (extraStripeForDiagonalMove) {
                --y;
            }

            break;
        }

        NewClientView *view = &(World::get()->clientview);

        for (z = - 2; z <= 2; ++z) {
            e = (direction != lower && z > 0) ? z*3 : 0; // left, right and upper stripes moved up if z>0 to provide the client with info for detecting roofs
            l = (dir == NewClientView::dir_down && z > 0) ? e : 0; // right and left stripes have to become longer then

            if (extraStripeForDiagonalMove) {
                ++l;
            }

            view->fillStripe(position(x-z*3+e,y+z*3-e,pos.z+z), dir, length+l, World::get()->maps);

            if (view->getExists()) {
                Connection->addCommand(boost::shared_ptr<BasicServerCommand>(new MapStripeTC(position(x-z*3+e,y+z*3-e,pos.z+z), dir)));
            }
        }
    }
}

void Player::sendStepStripes(direction dir) {
    switch (dir) {
    case(dir_north):
        //bewegung nach norden (Mapstripe links und oben)
        sendDirStripe(upper, false);
        sendDirStripe(left, false);
        break;
    case(dir_northeast):
        //bewegung nach nordosten (Mapstripe oben)
        sendDirStripe(upper, true);
        sendDirStripe(upper, false);
        break;
    case(dir_east) :
        //bewegung nach osten (Mapstripe oben und rechts)
        sendDirStripe(upper, false);
        sendDirStripe(right, false);
        break;
    case(dir_southeast):
        //bewegung suedosten (Mapstripe  rechts)
        sendDirStripe(right, true);
        sendDirStripe(right, false);
        break;
    case(dir_south):
        //bewegung sueden (Mapstripe rechts und unten)
        sendDirStripe(right, false);
        sendDirStripe(lower, false);
        break;
    case(dir_southwest):
        //bewegung suedwesten ( Mapstripe unten )
        sendDirStripe(lower, true);
        sendDirStripe(lower, false);
        break;
    case(dir_west):
        //bewegung westen ( Mapstripe unten und links)
        sendDirStripe(lower, false);
        sendDirStripe(left, false);
        break;
    case(dir_northwest):
        //bewegung nordwesten ( Mapstripe links )
        sendDirStripe(left, true);
        sendDirStripe(left, false);
        break;
    case(dir_up):
    case(dir_down):
        break;
    }
}

void Player::sendSingleStripe(viewdir direction, int8_t zoffs) {
    /* NOT USED YET
       int x,y,z;
       NewClientView::stripedirection dir;
       switch ( direction )
       {
           case upper:
               x = pos.x;
               y = pos.y - MAP_DIMENSION;
               dir = NewClientView::dir_right;
               break;
           case left:
               x = pos.x;
               y = pos.y - MAP_DIMENSION;
               dir = NewClientView::dir_down;
               break;
           case right:
               x = pos.x + MAP_DIMENSION;
               y = pos.y;
               dir = NewClientView::dir_down;
               break;
           case lower:
               x = pos.x - MAP_DIMENSION;
               y = pos.y;
               dir = NewClientView::dir_right;
               break;
       }
       z = pos.z + zoffs;
       NewClientView * view = &(World::get()->clientview);
       view->fillStripe( position(x,y,z), dir, &CWWorld::get()->maps );
    */
}

uint32_t Player::idleTime() {
    time_t now;
    time(&now);
    return now - lastaction;
}

void Player::sendBook(uint16_t bookID) {
    boost::shared_ptr<BasicServerCommand>cmd(new BookTC(bookID));
    Connection->addCommand(cmd);
}

std::string &Player::nls(std::string &german, std::string &english) {
    switch (getPlayerLanguage()) {
    case Language::german:
        return german;
    case Language::english:
        return english;
    default:
        return english;
    }
}

void Player::checkBurden() {
    if (LoadWeight() * 100 / maxLoadWeight() > 75) {
        if (!wasEncumberedSent()) {
            setEncumberedSent(true);
            std::string german = "Deine Last bremst dich.";
            std::string english = "Your burden slows you down.";
            informLua(nls(german, english));
        }
    } else if (wasEncumberedSent()) {
        setEncumberedSent(false);
        std::string german = "Eine schwere Last ist von deinen Schultern genommen.";
        std::string english = "A heavy burden has been lifted from your shoulders.";
        informLua(nls(german, english));
    }
}

bool Player::pageGM(std::string ticket) {
    try {
        _world->logGMTicket(this, "[Auto] " + ticket, "Automatic page about " + name + ": ");
        return true;
    } catch (...) {
    }

    return false;
}


void Player::sendCharDescription(TYPE_OF_CHARACTER_ID id,const std::string &desc) {
    boost::shared_ptr<BasicServerCommand>cmd(new CharDescription(id, desc));
    Connection->addCommand(cmd);
}

void Player::sendCharAppearance(TYPE_OF_CHARACTER_ID id, boost::shared_ptr<BasicServerCommand> appearance, bool always) {
    //send appearance always or only if the char in question just appeared
    if (always || visibleChars.insert(id).second) {
        Connection->addCommand(appearance);
    }
}

void Player::sendCharRemove(TYPE_OF_CHARACTER_ID id, boost::shared_ptr<BasicServerCommand> removechar) {
    if (this->id != id) {
        visibleChars.erase(id);
        Connection->addCommand(removechar);
    }
}

void Player::requestInputDialog(InputDialog *inputDialog) {
    requestDialog<InputDialog, InputDialogTC>(inputDialog);
}

void Player::executeInputDialog(unsigned int dialogId, bool success, std::string input) {
    InputDialog *inputDialog = getDialog<InputDialog>(dialogId);

    if (inputDialog != 0) {
        inputDialog->setSuccess(success);

        if (success) {
            inputDialog->setInput(input);
        }

        LuaScript::executeDialogCallback(*inputDialog);
    }

    delete inputDialog;
    dialogs.erase(dialogId);
}

void Player::requestMessageDialog(MessageDialog *messageDialog) {
    requestDialog<MessageDialog, MessageDialogTC>(messageDialog);
}

void Player::executeMessageDialog(unsigned int dialogId) {
    MessageDialog *messageDialog = getDialog<MessageDialog>(dialogId);

    if (messageDialog != 0) {
        LuaScript::executeDialogCallback(*messageDialog);
    }

    delete messageDialog;
    dialogs.erase(dialogId);
}

void Player::requestMerchantDialog(MerchantDialog *merchantDialog) {
    requestDialog<MerchantDialog, MerchantDialogTC>(merchantDialog);
}

void Player::executeMerchantDialogAbort(unsigned int dialogId) {
    MerchantDialog *merchantDialog = getDialog<MerchantDialog>(dialogId);

    if (merchantDialog != 0) {
        merchantDialog->setResult(MerchantDialog::playerAborts);
        merchantDialog->setPurchaseIndex(0);
        merchantDialog->setPurchaseAmount(0);
        ScriptItem item;
        merchantDialog->setSaleItem(item);
        LuaScript::executeDialogCallback(*merchantDialog);
    }

    delete merchantDialog;
    dialogs.erase(dialogId);
}

void Player::executeMerchantDialogBuy(unsigned int dialogId, MerchantDialog::index_type index, Item::number_type amount) {
    MerchantDialog *merchantDialog = getDialog<MerchantDialog>(dialogId);

    if (merchantDialog != 0) {
        merchantDialog->setResult(MerchantDialog::playerBuys);
        merchantDialog->setPurchaseIndex(index);
        merchantDialog->setPurchaseAmount(amount);
        ScriptItem item;
        merchantDialog->setSaleItem(item);
        LuaScript::executeDialogCallback(*merchantDialog);
    }
}

void Player::executeMerchantDialogSell(unsigned int dialogId, uint8_t location, TYPE_OF_CONTAINERSLOTS slot, Item::number_type amount) {
    MerchantDialog *merchantDialog = getDialog<MerchantDialog>(dialogId);

    if (merchantDialog != 0) {
        merchantDialog->setResult(MerchantDialog::playerSells);
        merchantDialog->setPurchaseIndex(0);
        merchantDialog->setPurchaseAmount(0);
        ScriptItem item;

        if (location == 0) {
            item = GetItemAt(slot);
        } else {
            if (isShowcaseOpen(location-1)) {
                Container *container = 0;
                getShowcaseContainer(location-1)->viewItemNr(slot, item, container);
            }
        }

        if (amount < item.getNumber()) {
            item.setNumber(amount);
        }

        merchantDialog->setSaleItem(item);
        LuaScript::executeDialogCallback(*merchantDialog);
    }
}

void Player::requestSelectionDialog(SelectionDialog *selectionDialog) {
    requestDialog<SelectionDialog, SelectionDialogTC>(selectionDialog);
}

void Player::executeSelectionDialog(unsigned int dialogId, bool success, SelectionDialog::index_type index) {
    SelectionDialog *selectionDialog = getDialog<SelectionDialog>(dialogId);

    if (selectionDialog != 0) {
        selectionDialog->setSuccess(success);

        if (success) {
            selectionDialog->setSelectedIndex(index);
        }

        LuaScript::executeDialogCallback(*selectionDialog);
    }

    delete selectionDialog;
    dialogs.erase(dialogId);
}

void Player::requestCraftingDialog(CraftingDialog *craftingDialog) {
    requestDialog<CraftingDialog, CraftingDialogTC>(craftingDialog);
}

void Player::executeCraftingDialogAbort(unsigned int dialogId) {
    CraftingDialog *craftingDialog = getDialog<CraftingDialog>(dialogId);

    if (craftingDialog != 0) {
        craftingDialog->setResult(CraftingDialog::playerAborts);
        LuaScript::executeDialogCallback(*craftingDialog);
    }

    delete craftingDialog;
    dialogs.erase(dialogId);
}

void Player::executeCraftingDialogCraft(unsigned int dialogId, uint8_t craftId, uint8_t craftAmount) {
    CraftingDialog *craftingDialog = getDialog<CraftingDialog>(dialogId);

    if (craftingDialog != 0) {
        craftingDialog->setResult(CraftingDialog::playerCrafts);
        craftingDialog->setCraftableId(craftId);
        craftingDialog->setCraftableAmount(craftAmount);
        bool craftingPossible = LuaScript::executeDialogCallback<bool>(*craftingDialog);

        if (craftingPossible) {
            abortAction();

            auto stillToCraft = craftingDialog->getCraftableAmount();
            auto craftingTime = craftingDialog->getCraftableTime();
            auto sfx = craftingDialog->getSfx();
            auto sfxDuration = craftingDialog->getSfxDuration();
            startCrafting(stillToCraft, craftingTime, sfx, sfxDuration, dialogId);
        }
    }
}

void Player::executeCraftingDialogCraftingComplete(unsigned int dialogId) {
    CraftingDialog *craftingDialog = getDialog<CraftingDialog>(dialogId);

    if (craftingDialog != 0) {
        craftingDialog->setResult(CraftingDialog::playerCraftingComplete);
        bool renewProductList = LuaScript::executeDialogCallback<bool>(*craftingDialog);

        boost::shared_ptr<BasicServerCommand> cmd(new CraftingDialogCraftingCompleteTC(dialogId));
        Connection->addCommand(cmd);

        if (renewProductList) {
            boost::shared_ptr<BasicServerCommand> cmd(new CraftingDialogTC(*craftingDialog, dialogId));
            Connection->addCommand(cmd);
        }

        auto stillToCraft = craftingDialog->getCraftableAmount() - 1;

        if (stillToCraft > 0) {
            auto craftId = craftingDialog->getCraftableId();
            executeCraftingDialogCraft(dialogId, craftId, stillToCraft);
        }
    }
}

void Player::executeCraftingDialogCraftingAborted(unsigned int dialogId) {
    CraftingDialog *craftingDialog = getDialog<CraftingDialog>(dialogId);

    if (craftingDialog != 0) {
        craftingDialog->setResult(CraftingDialog::playerCraftingAborted);
        LuaScript::executeDialogCallback(*craftingDialog);

        boost::shared_ptr<BasicServerCommand> cmd(new CraftingDialogCraftingAbortedTC(dialogId));
        Connection->addCommand(cmd);
    }
}

void Player::executeCraftingDialogLookAtCraftable(unsigned int dialogId, uint8_t craftId) {
    CraftingDialog *craftingDialog = getDialog<CraftingDialog>(dialogId);

    if (craftingDialog != 0) {
        craftingDialog->setResult(CraftingDialog::playerLooksAtCraftable);
        craftingDialog->setCraftableId(craftId);
        ItemLookAt lookAt = LuaScript::executeDialogCallback<ItemLookAt>(*craftingDialog);
        requestCraftingLookAt(dialogId, lookAt);
    }
}

void Player::executeCraftingDialogLookAtIngredient(unsigned int dialogId, uint8_t craftId, uint8_t craftIngredient) {
    CraftingDialog *craftingDialog = getDialog<CraftingDialog>(dialogId);

    if (craftingDialog != 0) {
        craftingDialog->setResult(CraftingDialog::playerLooksAtIngredient);
        craftingDialog->setCraftableId(craftId);
        craftingDialog->setIngredientIndex(craftIngredient);
        ItemLookAt lookAt = LuaScript::executeDialogCallback<ItemLookAt>(*craftingDialog);
        requestCraftingLookAtIngredient(dialogId, lookAt);
    }
}

void Player::requestCraftingLookAt(unsigned int dialogId, ItemLookAt &lookAt) {
    CraftingDialog *craftingDialog = getDialog<CraftingDialog>(dialogId);

    if (craftingDialog != 0) {
        boost::shared_ptr<BasicServerCommand> cmd(new LookAtDialogItemTC(dialogId, craftingDialog->getCraftableId(), lookAt));
        Connection->addCommand(cmd);
    }
}

void Player::requestCraftingLookAtIngredient(unsigned int dialogId, ItemLookAt &lookAt) {
    CraftingDialog *craftingDialog = getDialog<CraftingDialog>(dialogId);

    if (craftingDialog != 0) {
        boost::shared_ptr<BasicServerCommand> cmd(new LookAtCraftingDialogIngredientTC(dialogId, craftingDialog->getCraftableId(), craftingDialog->getIngredientIndex(), lookAt));
        Connection->addCommand(cmd);
    }
}

void Player::startCrafting(uint8_t stillToCraft, uint16_t craftingTime, uint16_t sfx, uint16_t sfxDuration, uint32_t dialogId) {
    SouTar source;
    source.Type = LUA_DIALOG;
    source.dialog = dialogId;
    SouTar target;
    ltAction->setLastAction(boost::shared_ptr<LuaScript>(), source, target, LongTimeAction::ACTION_CRAFT);
    startAction(craftingTime, 0, 0, sfx, sfxDuration);

    boost::shared_ptr<BasicServerCommand> cmd(new CraftingDialogCraftTC(stillToCraft, craftingTime, dialogId));
    Connection->addCommand(cmd);
}

void Player::invalidateDialogs() {
    for (auto it = dialogs.begin(); it != dialogs.end(); ++it) {
        if (it->second != 0) {
            boost::shared_ptr<BasicServerCommand> cmd(new CloseDialogTC(it->first));
            Connection->addCommand(cmd);
            delete it->second;
            it->second = 0;
        }
    }

    dialogs.clear();
}

void Player::closeDialogsOnMove() {
    for (auto it = dialogs.begin(); it != dialogs.end();) {
        if (it->second && it->second->closeOnMove()) {
            boost::shared_ptr<BasicServerCommand> cmd(new CloseDialogTC(it->first));
            Connection->addCommand(cmd);
            delete it->second;
            it->second = 0;
            it = dialogs.erase(it);
        } else if (it->second == 0) {
            it = dialogs.erase(it);
        } else {
            ++it;
        }
    }
}

