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
#include "Player.hpp"
#include "Item.hpp"
#include "Field.hpp"
#include "data/NaturalArmorTable.hpp"
#include "netinterface/protocol/ServerCommands.hpp"
#include "netinterface/protocol/BBIWIServerCommands.hpp"
#include "Logger.hpp"
#include "fuse_ptr.hpp"

class Character;

bool World::deleteNPC(unsigned int npcid) {
    /*
    int posx,posy,posz;
    Field* tempf; //alte NPC's l?chen
    NPCVECTOR::iterator npcIterator;
    for ( npcIterator = Npc.begin(); npcIterator < Npc.end(); ++npcIterator ) {
        if (( *npcIterator )->id == npcid) {
            if ( GetPToCFieldAt( tempf, ( *npcIterator )->pos.x, ( *npcIterator )->pos.y, ( *npcIterator )->pos.z ) ) {
                //tempf->SetNPCOnField( false );
                tempf->removeChar();
            }

            sendRemoveCharToVisiblePlayers( ( *npcIterator )->id, ( *npcIterator )->pos );
            delete( *npcIterator );
            Npc.erase( npcIterator );
            return true;
        }
    }
    return false;*/
    LostNpcs.push_back(npcid);
    return true;
}

bool World::createDynamicNPC(const std::string &name, Character::race_type type, const position &pos, /*CCharacter::face_to dir,*/ Character::sex_type sex, const std::string &scriptname) {
    try {

        try {
            NPC *newNPC = new NPC(DYNNPC_BASE, name, type, pos, (Character::face_to)4/*dir*/, false, sex, {});

            // add npc to npc list
            Npc.push_back(newNPC);

            // set field to occupied
            Field *tempf;

            if (GetPToCFieldAt(tempf, pos)) {
                tempf->setChar();
            }

            try {
                // try to load the script
                std::shared_ptr<LuaNPCScript> script(new LuaNPCScript(scriptname, newNPC));
                newNPC->setScript(script);
            } catch (ScriptException &e) {
                Logger::error(LogFacility::Script) << "World::createDynamicNPC: Error while loading dynamic NPC script: " << scriptname << ": " << e.what() << Log::end;
            }
        } catch (NoSpace &s) {
            Logger::error(LogFacility::Script) << "World::createDynamicNPC: No space available for dynamic NPC: " << name << ": " << s.what() << Log::end;
        }

        return true;
    } catch (...) {
        std::cerr << "World::createDynamicNPC: Unknown error while loading dynamic NPC: " << name << std::endl;
        return false;
    }
}


const World::PLAYERVECTOR& World::getPlayersOnline() const {
	return Players;
}

const World::NPCVECTOR& World::getNPCS() const {
	return Npc;
}

std::vector<Character*> World::getCharactersInRangeOf(const position &posi, uint8_t range) const {
    std::vector<Character*> list;

    std::vector < Player * > tempP = Players.findAllCharactersInRangeOf(posi.x , posi.y, posi.z, range);
    list.insert(list.end(), tempP.begin(), tempP.end());

    std::vector < Monster * > tempM = Monsters.findAllCharactersInRangeOf(posi.x , posi.y, posi.z, range);
    list.insert(list.end(), tempM.begin(), tempM.end());

    std::vector < NPC * > tempN = Npc.findAllCharactersInRangeOf(posi.x , posi.y, posi.z, range);
    list.insert(list.end(), tempN.begin(), tempN.end());

    return list;
}

std::vector<Player*> World::getPlayersInRangeOf(const position &posi, uint8_t range) const {
    return Players.findAllCharactersInRangeOf(posi.x , posi.y, posi.z, range);
}

std::vector<Monster*> World::getMonstersInRangeOf(const position &posi, uint8_t range) const {
    return Monsters.findAllCharactersInRangeOf(posi.x , posi.y, posi.z, range);
}

std::vector<NPC*> World::getNPCSInRangeOf(const position &posi, uint8_t range) const {
    return Npc.findAllCharactersInRangeOf(posi.x , posi.y, posi.z, range);
}

void World::itemInform(Character *user, ScriptItem item, ItemLookAt lookAt) {
    if (user->character != Character::player) {
        return;
    }

    Player *cp = dynamic_cast<Player *>(user);

    if (item.type == ScriptItem::it_container) {
        if (item.inside) {
            try {
                uint8_t showcase = cp->getShowcaseId(item.inside);
                ServerCommandPointer cmd(new LookAtShowCaseItemTC(showcase, item.itempos, lookAt));
                cp->Connection->addCommand(cmd);
            } catch (std::logic_error &) {
            }
        }
    } else if (item.type == ScriptItem::it_inventory || item.type == ScriptItem::it_belt) {
        ServerCommandPointer cmd(new LookAtInventoryItemTC(item.itempos, lookAt));
        cp->Connection->addCommand(cmd);
    } else if (item.type == ScriptItem::it_field) {
        ServerCommandPointer cmd(new LookAtMapItemTC(item.pos.x, item.pos.y, item.pos.z, lookAt));
        cp->Connection->addCommand(cmd);
    }
}


void World::changeQuality(ScriptItem item, short int amount) {
    short int tmpQuality = ((amount+item.getDurability())<100) ? (amount + item.getQuality()) : (item.getQuality() - item.getDurability() + 99);

    if (tmpQuality%100 > 0) {
        item.setQuality(tmpQuality);
        changeItem(item);
    } else {
        erase(item, item.getNumber());
    }
}

void World::changeQualityOfItemAt(const position &pos, short int amount) {
    Field *field;

    if (GetPToCFieldAt(field, pos.x, pos.y, pos.z)) {
        if (field->changeQualityOfTopItem(amount)) {
            sendRemoveItemFromMapToAllVisibleCharacters(0, pos.x, pos.y, pos.z, field);
        }
    }
}

bool World::changeItem(ScriptItem item) {
    if (item.type == ScriptItem::it_inventory || item.type == ScriptItem::it_belt) {
        item.owner->characterItems[ item.itempos ] = (Item)item;

        //Wenn character ein Spieler ist ein update schicken
        if (item.owner->character == Character::player) {
            dynamic_cast<Player *>(item.owner)->sendCharacterItemAtPos(item.itempos);
        }

        item.owner->updateAppearanceForAll(true);
        return true;
    } else if (item.type == ScriptItem::it_field) {
        Field *field;
        Item dummy;

        if (GetPToCFieldAt(field, item.pos.x, item.pos.y, item.pos.z)) {
            Item it;

            if (field->TakeTopItem(it)) {
                field->PutTopItem(static_cast<Item>(item));

                if (item.getId() != it.getId() || it.getNumber() != item.getNumber()) {
                    sendSwapItemOnMapToAllVisibleCharacter(it.getId(), item.pos.x, item.pos.y, item.pos.z, item, field);
                }
            }

            return true;
        } else {
            return false;
        }
    } else if (item.type == ScriptItem::it_container) {
        if (item.inside) {
            item.inside->changeItem(item);
            sendChangesOfContainerContentsIM(item.inside);
            return true;
        }
    }

    return false;
}

std::string World::getItemName(TYPE_OF_ITEM_ID itemid, uint8_t language) {
    if (language == 0) {
        return Data::ItemNames[itemid].German;
    } else {
        return Data::ItemNames[itemid].English;
    }
}


CommonStruct World::getItemStats(ScriptItem item) {
    const auto &data = Data::CommonItems[item.getId()];
    return data;
}

CommonStruct World::getItemStatsFromId(TYPE_OF_ITEM_ID id) {
    const auto &data = Data::CommonItems[id];
    return data;
}

bool World::isCharacterOnField(const position &pos) {
    if (findCharacterOnField(pos.x, pos.y, pos.z)) {
        return true;
    } else {
        return false;
    }
}

fuse_ptr<Character> World::getCharacterOnField(const position &pos) {
    return fuse_ptr<Character>(findCharacterOnField(pos.x, pos.y, pos.z));
}

bool World::erase(ScriptItem item, int amount) {
    if (amount > item.getNumber()) {
        amount = item.getNumber();
    }

    if (item.type == ScriptItem::it_inventory || item.type == ScriptItem::it_belt) {
        //Wenn Item rechts belegt und links ein Belegt ist [Zweihanditem] das Belegt mit l�schen
        if (item.itempos == RIGHT_TOOL && (item.owner->GetItemAt(LEFT_TOOL)).getId() == BLOCKEDITEM) {
            item.owner->increaseAtPos(LEFT_TOOL, -255);
        } else if (item.itempos == LEFT_TOOL && (item.owner->GetItemAt(RIGHT_TOOL)).getId() == BLOCKEDITEM) {
            item.owner->increaseAtPos(RIGHT_TOOL, -255);
        }

        item.owner->increaseAtPos(item.itempos, -amount);
        return true;
    }
    //Item befindet sich auf einen Feld am Boden liegend.
    else if (item.type == ScriptItem::it_field) {
        Field *field;
        Item dummy;

        if (GetPToCFieldAt(field, item.pos.x, item.pos.y, item.pos.z)) {
            bool erased=false;
            field->increaseTopItem(-amount, erased);

            if (erased) {
                sendRemoveItemFromMapToAllVisibleCharacters(0, item.pos.x, item.pos.y, item.pos.z, field);
            }

            return true;
        } else {
            logMissingField("erase", item.pos);
            return false;
        }
    } else if (item.type == ScriptItem::it_container) {
        if (item.inside) {
            item.inside->increaseAtPos(item.itempos, -amount);
            sendChangesOfContainerContentsIM(item.inside);
            return true;
        } else {
            return false;
        }
    }

    return false;
}


bool World::increase(ScriptItem item, short int count) {
    if (item.type == ScriptItem::it_inventory || item.type == ScriptItem::it_belt) {
        item.owner->increaseAtPos(item.itempos, count);
        return true;
    }
    //Item befindet sich auf einen Feld am Boden liegend.
    else if (item.type == ScriptItem::it_field) {
        Field *field;

        if (GetPToCFieldAt(field, item.pos.x, item.pos.y, item.pos.z)) {
            bool erased=false;
            field->increaseTopItem(count, erased);

            if (erased) {
                sendRemoveItemFromMapToAllVisibleCharacters(0, item.pos.x, item.pos.y, item.pos.z, field);
            }

            return true;
        } else {
            logMissingField("increase", item.pos);
            return false;
        }
    }

    else if (item.type == ScriptItem::it_container) {
        if (item.inside) {
            item.inside->increaseAtPos(item.itempos, count);
            sendChangesOfContainerContentsIM(item.inside);
            return true;
        } else {
            return false;
        }
    }

    return false;
}

bool World::swap(ScriptItem item, TYPE_OF_ITEM_ID newitem, unsigned short int newQuality) {
    if (item.type == ScriptItem::it_inventory || item.type == ScriptItem::it_belt) {
        item.owner->swapAtPos(item.itempos, newitem, newQuality);
        return true;
    }
    //Item befindet sich auf einen Feld am Boden liegend.
    else if (item.type == ScriptItem::it_field) {
        Field *field;

        if (GetPToCFieldAt(field, item.pos.x, item.pos.y, item.pos.z)) {
            Item it;

            if (field->ViewTopItem(it)) {
                bool ok = field->swapTopItem(newitem, newQuality);

                if (ok) {
                    Item dummy;
                    dummy.setId(newitem);
                    dummy.setNumber(it.getNumber());

                    if (it.getId() != newitem) {
                        sendSwapItemOnMapToAllVisibleCharacter(it.getId(), item.pos.x, item.pos.y, item.pos.z, dummy, field);
                    }
                } else {
                    Logger::error(LogFacility::Script) << "World::swap: Swapping item on Field (" << item.pos.x << ", " << item.pos.y << ", " << item.pos.z << ") failed!" << Log::end;
                    return false;
                }
            }
        } else {
            logMissingField("swap", item.pos);
            return false;
        }
    }

    else if (item.type == ScriptItem::it_container) {
        if (item.inside) {
            item.inside->swapAtPos(item.itempos, newitem, newQuality);
            sendChangesOfContainerContentsIM(item.inside);
            return true;
        } else {
            return false;
        }
    }


    return false;
}

ScriptItem World::createFromId(TYPE_OF_ITEM_ID id, unsigned short int count, const position &pos, bool always, int quality, script_data_exchangemap const *data) {
    Field *field;
    ScriptItem sItem;

    if (GetPToCFieldAt(field, pos.x, pos.y, pos.z)) {
        const auto &com = Data::CommonItems[id];

        if (com.isValid()) {
            g_item.setId(id);
            g_item.setNumber(count);
            g_item.setWear(com.AgeingSpeed);
            g_item.setQuality(quality);
            g_item.setData(data);
            g_cont = NULL;
            sItem = g_item;
            sItem.pos = pos;
            sItem.type = ScriptItem::it_field;
            sItem.itempos = 255;
            sItem.owner = NULL;

            if (always) {
                putItemAlwaysOnMap(NULL,pos.x,pos.y,pos.z);
            } else {
                putItemOnMap(NULL,pos.x,pos.y,pos.z);
            }

            return sItem;
        } else {
            Logger::error(LogFacility::Script) << "World::createFromId: Item " << id << " was not found in CommonItems!" << Log::end;
            return sItem;
        }
    } else {
        logMissingField("createFromId", pos);
        return sItem;
    }

    return sItem;

}

bool World::createFromItem(ScriptItem item, const position &pos, bool always) {
    Field *field;

    if (GetPToCFieldAt(field, pos.x, pos.y, pos.z)) {
        g_item = static_cast<Item>(item);
        g_cont = NULL;

        if (always) {
            putItemAlwaysOnMap(NULL,pos.x,pos.y,pos.z);
        } else {
            putItemOnMap(NULL,pos.x,pos.y,pos.z);
        }

        return true;
    } else {
        logMissingField("createFromItem", pos);
        return false;
    }

    return false;
}

fuse_ptr<Character> World::createMonster(unsigned short id, const position &pos, short movepoints) {
    Field *field;

    if (GetPToCFieldAt(field, pos.x, pos.y, pos.z)) {
        try {
            Monster *newMonster = new Monster(id, pos);
#ifdef LUASCRIPT_DEBUG
            std::cout<<"Erschaffe neues Monster: " << newMonster->name << " an Position (x,y,z) " << pos.x << " " << pos.y << " " << pos.z << std::endl;
#endif
            newMonster->actionPoints = movepoints;
            newMonsters.push_back(newMonster);
            field->setChar();
            sendCharacterMoveToAllVisiblePlayers(newMonster, NORMALMOVE, 4);
            return fuse_ptr<Character>(newMonster);

        } catch (Monster::unknownIDException &) {
            Logger::error(LogFacility::Script) << "World::createMonster: Failed to create monster with unknown id " << id << "!" << Log::end;
            return fuse_ptr<Character>();
        }
    } else {
        logMissingField("createMonster", pos);
        return fuse_ptr<Character>();
    }

    return fuse_ptr<Character>();
}

void World::gfx(unsigned short int gfxid, const position &pos) {
    std::vector < Player * > temp = Players.findAllCharactersInScreen(pos.x, pos.y, pos.z);
    std::vector < Player * > ::iterator titerator;

    for (titerator = temp.begin(); titerator < temp.end(); ++titerator) {
        ServerCommandPointer cmd(new GraphicEffectTC(pos.x, pos.y, pos.z, gfxid));
        (*titerator)->Connection->addCommand(cmd);
    }
}

void World::makeSound(unsigned short int soundid, const position &pos) {
    std::vector < Player * > temp = Players.findAllCharactersInScreen(pos.x, pos.y, pos.z);
    std::vector < Player * > ::iterator titerator;

    for (titerator = temp.begin(); titerator < temp.end(); ++titerator) {
        ServerCommandPointer cmd(new SoundTC(pos.x, pos.y, pos.z, soundid));
        (*titerator)->Connection->addCommand(cmd);
    }
}

bool World::isItemOnField(const position &pos) {
    Field *field;

    if (GetPToCFieldAt(field, pos.x, pos.y, pos.z)) {
        Item dummy;
        return field->ViewTopItem(dummy);
    } else {
        logMissingField("isItemOnField", pos);
    }

    return false;
}

ScriptItem World::getItemOnField(const position &pos) {
    Field *field;
    ScriptItem item;

    if (GetPToCFieldAt(field, pos.x, pos.y, pos.z)) {
        Item It;

        if (field->ViewTopItem(It)) {
            item = It;
            item.pos = pos;
            item.type = ScriptItem::it_field;
            return item;
        }
    } else {
        logMissingField("getItemOnField", pos);
    }

    return item;
}

void World::changeTile(short int tileid, const position &pos) {
    Field *field;

    if (GetPToCFieldAt(field, pos.x, pos.y, pos.z)) {
        field->setTileId(tileid);
        field->updateFlags();
    }
}


void World::sendMapUpdate(const position &pos, uint8_t range) {
    std::vector<Player *> temp;
    std::vector<Player *>::iterator pIterator;
    temp=Players.findAllCharactersInRangeOf(pos.x,pos.y,pos.z, range);

    for (pIterator = temp.begin(); pIterator != temp.end(); ++pIterator) {
        (*pIterator)->sendFullMap();
        sendAllVisibleCharactersToPlayer((*pIterator), true);
    }
}

bool World::createSavedArea(uint16_t tileid, const position &pos, uint16_t height, uint16_t width) {
    WorldMap::map_t dummy;

    for (time_t akt_x = pos.x; akt_x < pos.x+width; ++akt_x) {
        for (time_t akt_y = pos.y; akt_y < pos.y+height; ++akt_y) {
            if (maps.findMapForPos(akt_x, akt_y, pos.z, dummy)) {
                std::cerr<<"World::createSavedArea: Aborted map insertion, map for field at ("<<akt_x <<", "<<akt_y<<", "<<pos.z<<") found!"<<std::endl;
                return false;
            }
        }
    }

    WorldMap::map_t tempmap(new Map(width,height));
    tempmap->Init(pos.x, pos.y, pos.z);

    Field *tempf;

    for (int _x=0; _x<width; ++_x)
        for (int _y=0; _y<height; ++_y) {
            if (tempmap->GetPToCFieldAt(tempf, _x+pos.x, _y+pos.y)) {
                tempf->setTileId(tileid);
                tempf->updateFlags();
            } else {
                std::cerr << "World::createSavedArea: For map inserted at (" << pos.x << ", " << pos.y << ", " << pos.z << ") no Field was found for offset (" << _x << ", " << _y << ")!" << std::endl;
            }

        }

    maps.InsertMap(tempmap);
    std::cout<<" Map Created by createSavedArea command at x: "<<pos.x<<" y: "<<pos.y<<" z: "<<pos.z<<" height: "<<height<<" width: "<<width<<" standard tile: "<<tileid<<"!"<<std::endl;
    return true;
}


bool World::getArmorStruct(TYPE_OF_ITEM_ID id, ArmorStruct &ret) {
    //Has to be an own function cant give a pointer of Armor items to the script

    if (Data::ArmorItems.exists(id)) {
        ret = Data::ArmorItems[id];
        return true;
    } else {
        return false;
    }
}

bool World::getWeaponStruct(TYPE_OF_ITEM_ID id, WeaponStruct &ret) {
    //Has to be an own function cant give a pointer of Armor items to the script

    if (Data::WeaponItems.exists(id)) {
        ret = Data::WeaponItems[id];
        return true;
    } else {
        return false;
    }
}

bool World::getNaturalArmor(Character::race_type id, MonsterArmor &ret) {

    if (Data::NaturalArmors.exists(id)) {
        ret = Data::NaturalArmors[id];
        return true;
    } else {
        return false;
    }
}

bool World::getMonsterAttack(Character::race_type id, AttackBoni &ret) {

    if (Data::MonsterAttacks.exists(id)) {
        ret = Data::MonsterAttacks[id];
        return true;
    } else {
        return false;
    }
}

void World::sendMonitoringMessage(const std::string &msg, unsigned char id) {
    //send this Command to all Monitoring Clients
    ServerCommandPointer cmd(new BBMessageTC(msg, id));
    monitoringClientList->sendCommand(cmd);
}

void World::logMissingField(const std::string &function, const position &field) {
    Logger::error(LogFacility::Script) << "World::" << function << ": Field " << field << " was not found!" << Log::end;
}

