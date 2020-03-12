/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "ultima/ultima4/ultima4.h"
#include "ultima/ultima4/stats.h"
#include "ultima/ultima4/armor.h"
#include "ultima/ultima4/context.h"
#include "ultima/ultima4/debug.h"
#include "ultima/ultima4/menu.h"
#include "ultima/ultima4/names.h"
#include "ultima/ultima4/player.h"
#include "ultima/ultima4/savegame.h"
#include "ultima/ultima4/spell.h"
#include "ultima/ultima4/tile.h"
#include "ultima/ultima4/weapon.h"

namespace Ultima {
namespace Ultima4 {

extern bool verbose;

/**
 * StatsArea class implementation
 */
StatsArea::StatsArea() : 
    _title(STATS_AREA_X * CHAR_WIDTH, 0 * CHAR_HEIGHT, STATS_AREA_WIDTH, 1),
    _mainArea(STATS_AREA_X * CHAR_WIDTH, STATS_AREA_Y * CHAR_HEIGHT, STATS_AREA_WIDTH, STATS_AREA_HEIGHT),
    _summary(STATS_AREA_X * CHAR_WIDTH, (STATS_AREA_Y + STATS_AREA_HEIGHT + 1) * CHAR_HEIGHT, STATS_AREA_WIDTH, 1),
    _view(STATS_PARTY_OVERVIEW)
{
    // Generate a formatted Common::String for each menu item,
    // and then add the item to the menu.  The Y value
    // for each menu item will be filled in later.
    for (int count=0; count < 8; count++)
    {
        char outputBuffer[16];
        snprintf(outputBuffer, sizeof(outputBuffer), "-%-11s%%s", getReagentName((Reagent)count));
        _reagentsMixMenu.add(count, new IntMenuItem(outputBuffer, 1, 0, -1, (int *)c->_party->getReagentPtr((Reagent)count), 0, 99, 1, MENU_OUTPUT_REAGENT));
    }

    _reagentsMixMenu.addObserver(this);
}
 
void StatsArea::setView(StatsView view) {
    this->_view = view;
    update();
}

/**
 * Sets the stats item to the previous in sequence.
 */
void StatsArea::prevItem() {
    _view = (StatsView)(_view - 1);
    if (_view < STATS_CHAR1)
        _view = STATS_MIXTURES;
    if (_view <= STATS_CHAR8 && (_view - STATS_CHAR1 + 1) > c->_party->size())
        _view = (StatsView) (STATS_CHAR1 - 1 + c->_party->size());
    update();
}

/**
 * Sets the stats item to the next in sequence.
 */
void StatsArea::nextItem() {
    _view = (StatsView)(_view + 1);    
    if (_view > STATS_MIXTURES)
        _view = STATS_CHAR1;
    if (_view <= STATS_CHAR8 && (_view - STATS_CHAR1 + 1) > c->_party->size())
        _view = STATS_WEAPONS;
    update();
}

/**
 * Update the stats (ztats) box on the upper right of the screen.
 */
void StatsArea::update(bool avatarOnly) {
    clear();

    /*
     * update the upper stats box
     */
    switch(_view) {
    case STATS_PARTY_OVERVIEW:
        showPartyView(avatarOnly);
        break;
    case STATS_CHAR1:
    case STATS_CHAR2:
    case STATS_CHAR3:
    case STATS_CHAR4:
    case STATS_CHAR5:
    case STATS_CHAR6:
    case STATS_CHAR7:
    case STATS_CHAR8:
        showPlayerDetails();
        break;
    case STATS_WEAPONS:
        showWeapons();
        break;
    case STATS_ARMOR:
        showArmor();
        break;
    case STATS_EQUIPMENT:
        showEquipment();
        break;
    case STATS_ITEMS:
        showItems();
        break;
    case STATS_REAGENTS:
        showReagents();
        break;
    case STATS_MIXTURES:
        showMixtures();
        break;
    case MIX_REAGENTS:
        showReagents(true);
        break;
    }

    /*
     * update the lower stats box (food, gold, etc.)
     */
    if (c->_transportContext == TRANSPORT_SHIP)
        _summary.textAt(0, 0, "F:%04d   SHP:%02d", c->_saveGame->_food / 100, c->_saveGame->_shipHull);
    else
        _summary.textAt(0, 0, "F:%04d   G:%04d", c->_saveGame->_food / 100, c->_saveGame->_gold);

    update(c->_aura);

    redraw();
}

void StatsArea::update(Aura *aura) {
    unsigned char mask = 0xff;
    for (int i = 0; i < VIRT_MAX; i++) {
        if (c->_saveGame->_karma[i] == 0)
            mask &= ~(1 << i);
    }

    switch (aura->getType()) {
    case Aura::NONE:
        _summary.drawCharMasked(0, STATS_AREA_WIDTH/2, 0, mask);
        break;
    case Aura::HORN:
        _summary.drawChar(CHARSET_REDDOT, STATS_AREA_WIDTH/2, 0);
        break;
    case Aura::JINX:
        _summary.drawChar('J', STATS_AREA_WIDTH/2, 0);
        break;
    case Aura::NEGATE:
        _summary.drawChar('N', STATS_AREA_WIDTH/2, 0);
        break;
    case Aura::PROTECTION:
        _summary.drawChar('P', STATS_AREA_WIDTH/2, 0);
        break;
    case Aura::QUICKNESS:
        _summary.drawChar('Q', STATS_AREA_WIDTH/2, 0);
        break;
    }    

    _summary.update();
}

void StatsArea::highlightPlayer(int player) {
    ASSERT(player < c->_party->size(), "player number out of range: %d", player);
    _mainArea.highlight(0, player * CHAR_HEIGHT, STATS_AREA_WIDTH * CHAR_WIDTH, CHAR_HEIGHT);
#ifdef IOS
    U4IOS::updateActivePartyMember(player);
#endif
}

void StatsArea::clear() {
    for (int i = 0; i < STATS_AREA_WIDTH; i++)
        _title.drawChar(CHARSET_HORIZBAR, i, 0);

    _mainArea.clear();
    _summary.clear();
}

/**
 * Redraws the entire stats area
 */
void StatsArea::redraw() {
    _title.update();
    _mainArea.update();
    _summary.update();
}

/**
 * Sets the title of the stats area.
 */
void StatsArea::setTitle(const Common::String &s) {
    int titleStart = (STATS_AREA_WIDTH / 2) - ((s.size() + 2) / 2);
    _title.textAt(titleStart, 0, "%c%s%c", 16, s.c_str(), 17);
}

/**
 * The basic party view.
 */
void StatsArea::showPartyView(bool avatarOnly) {
    const char *format = "%d%c%-9.8s%3d%s";

    PartyMember *p = NULL;
    int activePlayer = c->_party->getActivePlayer();

    ASSERT(c->_party->size() <= 8, "party members out of range: %d", c->_party->size());

    if (!avatarOnly) {
        for (int i = 0; i < c->_party->size(); i++) {
            p = c->_party->member(i);
            _mainArea.textAt(0, i, format, i+1, (i==activePlayer) ? CHARSET_BULLET : '-', p->getName().c_str(), p->getHp(), _mainArea.colorizeStatus(p->getStatus()).c_str());
        }
    }
    else {        
        p = c->_party->member(0);
        _mainArea.textAt(0, 0, format, 1, (activePlayer==0) ? CHARSET_BULLET : '-', p->getName().c_str(), p->getHp(), _mainArea.colorizeStatus(p->getStatus()).c_str());
    }
}

/**
 * The individual character view.
 */
void StatsArea::showPlayerDetails() {
    int player = _view - STATS_CHAR1;

    ASSERT(player < 8, "character number out of range: %d", player);

    PartyMember *p = c->_party->member(player);
    setTitle(p->getName());
    _mainArea.textAt(0, 0, "%c             %c", p->getSex(), p->getStatus());
    Common::String classStr = getClassName(p->getClass());
    int classStart = (STATS_AREA_WIDTH / 2) - (classStr.size() / 2);
    _mainArea.textAt(classStart, 0, "%s", classStr.c_str());
    _mainArea.textAt(0, 2, " MP:%02d  LV:%d", p->getMp(), p->getRealLevel());
    _mainArea.textAt(0, 3, "STR:%02d  HP:%04d", p->getStr(), p->getHp());
    _mainArea.textAt(0, 4, "DEX:%02d  HM:%04d", p->getDex(), p->getMaxHp());
    _mainArea.textAt(0, 5, "INT:%02d  EX:%04d", p->getInt(), p->getExp());
    _mainArea.textAt(0, 6, "W:%s", p->getWeapon()->getName().c_str());
    _mainArea.textAt(0, 7, "A:%s", p->getArmor()->getName().c_str());
}

/**
 * Weapons in inventory.
 */
void StatsArea::showWeapons() {
    setTitle("Weapons");

    int line = 0;
    int col = 0;
    _mainArea.textAt(0, line++, "A-%s", Weapon::get(WEAP_HANDS)->getName().c_str());
    for (int w = WEAP_HANDS + 1; w < WEAP_MAX; w++) {
        int n = c->_saveGame->_weapons[w];
        if (n >= 100)
            n = 99;
        if (n >= 1) {
            const char *format = (n >= 10) ? "%c%d-%s" : "%c-%d-%s";

            _mainArea.textAt(col, line++, format, w - WEAP_HANDS + 'A', n, Weapon::get((WeaponType) w)->getAbbrev().c_str());
            if (line >= (STATS_AREA_HEIGHT)) {
                line = 0;
                col += 8;
            }
        }
    }    
}

/**
 * Armor in inventory.
 */
void StatsArea::showArmor() {
    setTitle("Armour");

    int line = 0;
    _mainArea.textAt(0, line++, "A  -No Armour");
    for (int a = ARMR_NONE + 1; a < ARMR_MAX; a++) {
        if (c->_saveGame->_armor[a] > 0) {
            const char *format = (c->_saveGame->_armor[a] >= 10) ? "%c%d-%s" : "%c-%d-%s";

            _mainArea.textAt(0, line++, format, a - ARMR_NONE + 'A', c->_saveGame->_armor[a], Armor::get((ArmorType) a)->getName().c_str());
        }
    }
}

/**
 * Equipment: touches, gems, keys, and sextants.
 */
void StatsArea::showEquipment() {
    setTitle("Equipment");

    int line = 0;
    _mainArea.textAt(0, line++, "%2d Torches", c->_saveGame->_torches);
    _mainArea.textAt(0, line++, "%2d Gems", c->_saveGame->_gems);
    _mainArea.textAt(0, line++, "%2d Keys", c->_saveGame->_keys);
    if (c->_saveGame->_sextants > 0)
        _mainArea.textAt(0, line++, "%2d Sextants", c->_saveGame->_sextants);    
}

/**
 * Items: runes, stones, and other miscellaneous quest items.
 */
void StatsArea::showItems() {
    int i, j;
    char buffer[17];

    setTitle("Items");

    int line = 0;
    if (c->_saveGame->_stones != 0) {
        j = 0;
        for (i = 0; i < 8; i++) {
            if (c->_saveGame->_stones & (1 << i))
                buffer[j++] = getStoneName((Virtue) i)[0];
        }
        buffer[j] = '\0';
        _mainArea.textAt(0, line++, "Stones:%s", buffer);
    }
    if (c->_saveGame->_runes != 0) {
        j = 0;
        for (i = 0; i < 8; i++) {
            if (c->_saveGame->_runes & (1 << i))
                buffer[j++] = getVirtueName((Virtue) i)[0];
        }
        buffer[j] = '\0';
        _mainArea.textAt(0, line++, "Runes:%s", buffer);
    }
    if (c->_saveGame->_items & (ITEM_CANDLE | ITEM_BOOK | ITEM_BELL)) {
        buffer[0] = '\0';
        if (c->_saveGame->_items & ITEM_BELL) {
            strcat(buffer, getItemName(ITEM_BELL));
            strcat(buffer, " ");
        }
        if (c->_saveGame->_items & ITEM_BOOK) {
            strcat(buffer, getItemName(ITEM_BOOK));
            strcat(buffer, " ");
        }
        if (c->_saveGame->_items & ITEM_CANDLE) {
            strcat(buffer, getItemName(ITEM_CANDLE));
            buffer[15] = '\0';
        }
        _mainArea.textAt(0, line++, "%s", buffer);
    }
    if (c->_saveGame->_items & (ITEM_KEY_C | ITEM_KEY_L | ITEM_KEY_T)) {
        j = 0;
        if (c->_saveGame->_items & ITEM_KEY_T)
            buffer[j++] = getItemName(ITEM_KEY_T)[0];
        if (c->_saveGame->_items & ITEM_KEY_L)
            buffer[j++] = getItemName(ITEM_KEY_L)[0];
        if (c->_saveGame->_items & ITEM_KEY_C)
            buffer[j++] = getItemName(ITEM_KEY_C)[0];
        buffer[j] = '\0';
        _mainArea.textAt(0, line++, "3 Part Key:%s", buffer);
    }
    if (c->_saveGame->_items & ITEM_HORN)
        _mainArea.textAt(0, line++, "%s", getItemName(ITEM_HORN));
    if (c->_saveGame->_items & ITEM_WHEEL)
        _mainArea.textAt(0, line++, "%s", getItemName(ITEM_WHEEL));
    if (c->_saveGame->_items & ITEM_SKULL)
        _mainArea.textAt(0, line++, "%s", getItemName(ITEM_SKULL));    
}

/**
 * Unmixed reagents in inventory.
 */
void StatsArea::showReagents(bool active)
{
    setTitle("Reagents");

    Menu::MenuItemList::iterator i;
    int line = 0,
        r = REAG_ASH;
    Common::String shortcut ("A");

    _reagentsMixMenu.show(&_mainArea);

    for (i = _reagentsMixMenu.begin(); i != _reagentsMixMenu.end(); i++, r++)
    {
        if ((*i)->isVisible())
        {
            // Insert the reagent menu item shortcut character
            shortcut.setChar('A' + r, 0);
            if (active)
                _mainArea.textAt(0, line++, "%s", _mainArea.colorizeString(shortcut, FG_YELLOW, 0, 1).c_str());
            else
                _mainArea.textAt(0, line++, "%s", shortcut.c_str());
        }
    }
}

/**
 * Mixed reagents in inventory.
 */
void StatsArea::showMixtures() {
    setTitle("Mixtures");

    int line = 0;
    int col = 0;
    for (int s = 0; s < SPELL_MAX; s++) {
        int n = c->_saveGame->_mixtures[s];
        if (n >= 100)
            n = 99;
        if (n >= 1) {
            _mainArea.textAt(col, line++, "%c-%02d", s + 'A', n);
            if (line >= (STATS_AREA_HEIGHT)) {
                if (col >= 10)
                    break;
                line = 0;
                col += 5;
            }
        }
    }
}

void StatsArea::resetReagentsMenu() {
    Menu::MenuItemList::iterator current;
    int i = 0,
        row = 0;

    for (current = _reagentsMixMenu.begin(); current != _reagentsMixMenu.end(); current++)
    {
        if (c->_saveGame->_reagents[i++] > 0)
        {
            (*current)->setVisible(true);
            (*current)->setY(row++);
        }
        else (*current)->setVisible(false);
    }

    _reagentsMixMenu.reset(false);
}

/**
 * Handles spell mixing for the Ultima V-style menu-system
 */
bool ReagentsMenuController::keyPressed(int key) {
    switch(key) {
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
        {
            /* select the corresponding reagent (if visible) */
            Menu::MenuItemList::iterator mi = _menu->getById(key-'a');
            if ((*mi)->isVisible()) {
                _menu->setCurrent(_menu->getById(key-'a'));
                keyPressed(U4_SPACE);
            }
        } break;
    case U4_LEFT:
    case U4_RIGHT:
    case U4_SPACE:
        if (_menu->isVisible()) {            
            MenuItem *item = *_menu->getCurrent();
            
            /* change whether or not it's selected */
            item->setSelected(!item->isSelected());
                        
            if (item->isSelected())
                _ingredients->addReagent((Reagent)item->getId());
            else
                _ingredients->removeReagent((Reagent)item->getId());
        }
        break;
    case U4_ENTER:
        eventHandler->setControllerDone();
        break;

    case U4_ESC:
        _ingredients->revert();
        eventHandler->setControllerDone();
        break;

    default:
        return MenuController::keyPressed(key);
    }

    return true;
}


} // End of namespace Ultima4
} // End of namespace Ultima