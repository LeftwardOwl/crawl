/*
 *  File:       quiver.cc
 *  Summary:    Player quiver functionality
 *
 *  Modified for Crawl Reference by $Author: j-p-e-g $ on $Date: 2008-06-23 23:30:08 +0200 (Mo, 23 Jun 2008) $
 *
 *  - Only change last_used when actually using
 *  - Not changing Qv; nobody knows about internals
 *  - Track last_used of each type so each weapon can do the right thing
 */

#include "AppHdr.h"
#include "quiver.h"

#include "invent.h"
#include "item_use.h"
#include "itemprop.h"
#include "items.h"
#include "stuff.h"
#include "tags.h"

#include <algorithm>

static int _get_pack_slot(const item_def&);
static ammo_t _get_weapon_ammo_type(const item_def*);
static bool _item_matches(const item_def &item, fire_type types,
                          const item_def* launcher);
static bool _items_similar(const item_def& a, const item_def& b,
                           bool force = true);

// ----------------------------------------------------------------------
// player_quiver
// ----------------------------------------------------------------------

player_quiver::player_quiver()
    : m_last_used_type(AMMO_THROW)
{
    COMPILE_CHECK(ARRAYSZ(m_last_used_of_type) == NUM_AMMO, a);
}

// Return:
//   *slot_out filled in with the inv slot of the item we would like
//   to fire by default.  If -1, the inv doesn't contain our desired
//   item.
//
//   *item_out filled in with item we would like to fire by default.
//   This can be returned even if the item is not in inv (although if
//   it is in inv, a reference to the inv item, with accurate count,
//   is returned)
//
// This is the item that will be displayed in Qv:
//
void player_quiver::get_desired_item(const item_def** item_out, int* slot_out) const
{
    const int slot = _get_pack_slot(m_last_used_of_type[m_last_used_type]);
    if (slot == -1)
    {
        // Not in inv, but caller can at least get the type of the item.
        if (item_out)
            *item_out = &m_last_used_of_type[m_last_used_type];
    }
    else
    {
        // Return the item in inv, since it will have an accurate count.
        if (item_out)
            *item_out = &you.inv[slot];
    }

    if (slot_out)
        *slot_out = slot;
}

// Return inv slot of item that should be fired by default.
// This differs from get_desired_item; that method can return
// an item that is not in inventory, while this one cannot.
// If no item can be found, return the reason why.
int player_quiver::get_fire_item(std::string* no_item_reason) const
{
    int slot;
    const item_def* desired_item;

    get_desired_item(&desired_item, &slot);

    // If not in inv, try the head of the fire order.
    if (slot == -1)
    {
        std::vector<int> order;
        _get_fire_order(order, false, you.weapon());
        if (order.size())
            slot = order[0];
    }

    // If we can't find anything, tell caller why.
    if (slot == -1)
    {
        std::vector<int> full_fire_order;
        _get_fire_order(full_fire_order, true, you.weapon());
        if (no_item_reason == NULL)
        {
            // nothing
        }
        else if (full_fire_order.size() == 0)
        {
            *no_item_reason = "No suitable missiles.";
        }
        else
        {
            const int skipped_item = full_fire_order[0];
            if (skipped_item < Options.fire_items_start)
            {
                *no_item_reason = make_stringf(
                    "Nothing suitable (fire_items_start = '%c').",
                    index_to_letter(Options.fire_items_start));
            }
            else
            {
                *no_item_reason = make_stringf(
                    "Nothing suitable (ignored '=f'-inscribed item on '%c').",
                    index_to_letter(skipped_item));
            }
        }
    }

    return slot;
}

void player_quiver::set_quiver(const item_def &item, ammo_t ammo_type)
{
    m_last_used_of_type[ammo_type] = item;
    m_last_used_of_type[ammo_type].quantity = 1;
    m_last_used_type  = ammo_type;
    you.redraw_quiver = true;
}

void player_quiver::empty_quiver(ammo_t ammo_type)
{
    m_last_used_of_type[ammo_type] = item_def();
    m_last_used_of_type[ammo_type].quantity = 0;
    m_last_used_type  = ammo_type;
    you.redraw_quiver = true;
}

void choose_item_for_quiver()
{
    int slot = prompt_invent_item( "Quiver which item? (- for none, * to show all)",
                                   MT_INVLIST,
                                   OSEL_THROWABLE, true, true, true, '-',
                                   you.equip[EQ_WEAPON], NULL, OPER_QUIVER );

    if (prompt_failed(slot))
        return;

    if (slot == PROMPT_GOT_SPECIAL)  // '-' or empty quiver
    {
        ammo_t t = _get_weapon_ammo_type(you.weapon());
        you.m_quiver->empty_quiver(t);

        mprf("Reset %s quiver to default.",
             t == AMMO_THROW    ? "throwing" :
             t == AMMO_BLOWGUN  ? "blowgun" :
             t == AMMO_SLING    ? "sling" :
             t == AMMO_BOW      ? "bow" :
             t == AMMO_CROSSBOW ? "crossbow"
                                : "hand crossbow");
        return;
    }
    else if (slot == you.equip[EQ_WEAPON])
    {
        mpr("You can't quiver wielded items.");
        return;
    }
    else
    {
        for (int i = 0; i < NUM_EQUIP; i++)
        {
            if (you.equip[i] == slot)
            {
                mpr("You can't quiver worn items.");
                return;
            }
        }
    }

    const item_def item = you.inv[slot];
    ASSERT(is_valid_item(item));

    ammo_t t = AMMO_THROW;
    const item_def *weapon = you.weapon();
    if (weapon && item.launched_by(*weapon))
        t = _get_weapon_ammo_type(weapon);

    you.m_quiver->set_quiver(you.inv[slot], t);
    mprf("Quivering %s for %s.", you.inv[slot].name(DESC_INVENTORY).c_str(),
         t == AMMO_THROW    ? "throwing" :
         t == AMMO_BLOWGUN  ? "blowguns" :
         t == AMMO_SLING    ? "slings" :
         t == AMMO_BOW      ? "bows" :
         t == AMMO_CROSSBOW ? "crossbows"
                            : "hand crossbows");
}

// Notification that item was fired with 'f'.
void player_quiver::on_item_fired(const item_def& item, bool explicitly_chosen)
{
    if (!explicitly_chosen)
    {
        // If the item was not actively chosen, i.e. just automatically
        // passed into the quiver, don't change any of the quiver settings.
        you.redraw_quiver = true;
        return;
    }
    // If item matches the launcher, put it in that launcher's last-used item.
    // Otherwise, it goes into last hand-thrown item.

    const item_def *weapon = you.weapon();

    if (weapon && item.launched_by(*weapon))
    {
        const ammo_t t = _get_weapon_ammo_type(weapon);
        m_last_used_of_type[t] = item;
        m_last_used_of_type[t].quantity = 1;    // 0 makes it invalid :(
        m_last_used_type = t;
    }
    else
    {
        const launch_retval projected = is_launched(&you, you.weapon(),
                                                    item);

        // Don't do anything if this item is not really fit for throwing.
        if (projected == LRET_FUMBLED)
            return;

#ifdef DEBUG_QUIVER
        mprf(MSGCH_DIAGNOSTICS, "item %s is for throwing",
             item.name(DESC_PLAIN).c_str());
#endif
        m_last_used_of_type[AMMO_THROW] = item;
        m_last_used_of_type[AMMO_THROW].quantity = 1;
        m_last_used_type = AMMO_THROW;
    }

    you.redraw_quiver = true;
}

// Notification that item was fired with 'f' 'i'
void player_quiver::on_item_fired_fi(const item_def& item)
{
    // Currently no difference.
    on_item_fired(item);
}

// Called when the player might have switched weapons, or might have
// picked up something interesting.
void player_quiver::on_weapon_changed()
{
    // Only switch m_last_used_type if weapon really changed
    const item_def* weapon = you.weapon();
    if (weapon == NULL)
    {
        if (m_last_weapon.base_type != OBJ_UNASSIGNED)
        {
            m_last_weapon.base_type = OBJ_UNASSIGNED;
            m_last_used_type = AMMO_THROW;
        }
    }
    else
    {
        if (!_items_similar(*weapon, m_last_weapon))
        {
            // Weapon type changed.
            m_last_weapon = *weapon;
            m_last_used_type = _get_weapon_ammo_type(weapon);
        }
    }

    _maybe_fill_empty_slot();
}

void player_quiver::on_inv_quantity_changed(int slot, int amt)
{
    const launch_retval projected = is_launched(&you, you.weapon(),
                                                you.inv[slot]);

    // Don't do anything if this item is not throwable.
    if (projected == LRET_FUMBLED)
        return;

    if (m_last_used_of_type[m_last_used_type].base_type == OBJ_UNASSIGNED)
    {
        // Empty quiver.  Maybe we can fill it now?
        _maybe_fill_empty_slot();
        you.redraw_quiver = true;
    }
    else
    {
        // We might need to update the quiver...
        int qv_slot = get_fire_item();
        if (qv_slot == slot)
            you.redraw_quiver = true;
    }
}

// If current quiver slot is empty, fill it with something useful.
void player_quiver::_maybe_fill_empty_slot()
{
    const item_def* weapon = you.weapon();
    const ammo_t slot = _get_weapon_ammo_type(weapon);

#ifdef DEBUG_QUIVER
    mprf(MSGCH_DIAGNOSTICS, "last quiver item: %s; link %d, wpn: %d",
         m_last_used_of_type[slot].name(DESC_PLAIN).c_str(),
         m_last_used_of_type[slot].link, you.equip[EQ_WEAPON]);
#endif

    bool unquiver_weapon = false;
    if (is_valid_item(m_last_used_of_type[slot]))
    {
        // If we're wielding an item previously quivered, the quiver may need
        // to be cleared. Else, any already quivered item is valid and we
        // don't need to do anything else.
        if (m_last_used_of_type[slot].link == you.equip[EQ_WEAPON])
            unquiver_weapon = true;
        else
            return;
    }

#ifdef DEBUG_QUIVER
    mpr("Recalculating fire order...", MSGCH_DIAGNOSTICS);
#endif

    const launch_retval desired_ret =
         (weapon && is_range_weapon(*weapon)) ? LRET_LAUNCHED : LRET_THROWN;
//    const launch_retval desired_ret =
//        (slot == AMMO_THROW ? LRET_THROWN : LRET_LAUNCHED);

    std::vector<int> order;
    _get_fire_order(order, false, weapon);

    if (unquiver_weapon && order.empty())
    {
        // Setting the quantity to zero will force the quiver to be empty,
        // should nothing else be found.
        m_last_used_of_type[slot].quantity = 0;
    }
    else
    {
        for (unsigned int i = 0; i < order.size(); i++)
        {
            if (is_launched(&you, weapon, you.inv[order[i]]) == desired_ret)
            {
                m_last_used_of_type[slot] = you.inv[order[i]];
                m_last_used_of_type[slot].quantity = 1;
                break;
            }
        }
    }
}

void player_quiver::get_fire_order(std::vector<int>& v) const
{
    _get_fire_order(v, false, you.weapon());
}

// Get a sorted list of items to show in the fire interface.
//
// If ignore_inscription_etc, ignore =f and Options.fire_items_start.
// This is used for generating informational error messages, when the
// fire order is empty.
//
// launcher determines what items match the 'launcher' fire_order type.
void player_quiver::_get_fire_order( std::vector<int>& order,
                                     bool ignore_inscription_etc,
                                     const item_def* launcher) const
{
    const int inv_start = (ignore_inscription_etc ? 0
                                                  : Options.fire_items_start);

    // If in a net, cannot throw anything, and can only launch from blowgun.
    if (you.attribute[ATTR_HELD])
    {
        if (launcher && launcher->sub_type == WPN_BLOWGUN)
        {
            for (int i_inv = inv_start; i_inv < ENDOFPACK; i_inv++)
                if (is_valid_item(you.inv[i_inv])
                    && you.inv[i_inv].launched_by(*launcher))
                {
                    order.push_back(i_inv);
                }
        }
        return;
    }

    for (int i_inv = inv_start; i_inv < ENDOFPACK; i_inv++)
    {
        const item_def& item = you.inv[i_inv];
        if (!is_valid_item(item))
            continue;

        // Don't quiver wielded weapon.
        if (you.equip[EQ_WEAPON] == i_inv)
            continue;

        // Don't do anything if this item is not really fit for throwing.
        if (is_launched(&you, you.weapon(), item) == LRET_FUMBLED)
            continue;

        // =f prevents item from being in fire order.
        if (!ignore_inscription_etc
            && strstr(item.inscription.c_str(), "=f"))
        {
            continue;
        }

        for (unsigned int i_flags = 0; i_flags < Options.fire_order.size();
             i_flags++)
        {

            if (_item_matches(item, (fire_type) Options.fire_order[i_flags],
                              launcher))
            {
                order.push_back( (i_flags<<16) | (i_inv & 0xffff) );
                break;
            }
        }
    }

    std::sort(order.begin(), order.end());

    for (unsigned int i = 0; i < order.size(); i++)
        order[i] &= 0xffff;
}

// ----------------------------------------------------------------------
// Save/load
// ----------------------------------------------------------------------

static const short QUIVER_COOKIE = short(0xb015);
void player_quiver::save(writer& outf) const
{
    marshallShort(outf, QUIVER_COOKIE);

    marshallItem(outf, m_last_weapon);
    marshallLong(outf, m_last_used_type);
    marshallLong(outf, ARRAYSZ(m_last_used_of_type));

    for (unsigned int i = 0; i < ARRAYSZ(m_last_used_of_type); i++)
        marshallItem(outf, m_last_used_of_type[i]);
}

void player_quiver::load(reader& inf)
{
    const short cooky = unmarshallShort(inf);
    ASSERT(cooky == QUIVER_COOKIE); (void)cooky;

    unmarshallItem(inf, m_last_weapon);
    m_last_used_type = (ammo_t)unmarshallLong(inf);
    ASSERT(m_last_used_type >= AMMO_THROW && m_last_used_type < NUM_AMMO);

    const unsigned long count = unmarshallLong(inf);
    ASSERT(count <= ARRAYSZ(m_last_used_of_type));

    for (unsigned int i = 0; i < count; i++)
        unmarshallItem(inf, m_last_used_of_type[i]);
}

// ----------------------------------------------------------------------
// Identify helper
// ----------------------------------------------------------------------

preserve_quiver_slots::preserve_quiver_slots()
{
    if (!you.m_quiver)
        return;

    COMPILE_CHECK(ARRAYSZ(m_last_used_of_type) ==
                  ARRAYSZ(you.m_quiver->m_last_used_of_type), a);

    for (unsigned int i = 0; i < ARRAYSZ(m_last_used_of_type); i++)
    {
        m_last_used_of_type[i] =
            _get_pack_slot(you.m_quiver->m_last_used_of_type[i]);
    }
}

preserve_quiver_slots::~preserve_quiver_slots()
{
    if (!you.m_quiver)
        return;

    for (unsigned int i = 0; i < ARRAYSZ(m_last_used_of_type); i++)
    {
        const int slot = m_last_used_of_type[i];
        if (slot != -1)
        {
            you.m_quiver->m_last_used_of_type[i] = you.inv[slot];
            you.m_quiver->m_last_used_of_type[i].quantity = 1;
        }
    }
    you.redraw_quiver = true;
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

// Helper for _get_fire_order.
// Types may actually contain more than one fire_type.
static bool _item_matches(const item_def &item, fire_type types,
                          const item_def* launcher)
{
    ASSERT(is_valid_item(item));

    if (types & FIRE_INSCRIBED)
        if (item.inscription.find("+f", 0) != std::string::npos)
            return (true);

    if (item.base_type == OBJ_MISSILES)
    {
        if ((types & FIRE_DART) && item.sub_type == MI_DART)
            return (true);
        if ((types & FIRE_STONE) && item.sub_type == MI_STONE)
            return (true);
        if ((types & FIRE_JAVELIN) && item.sub_type == MI_JAVELIN)
            return (true);
        if ((types & FIRE_ROCK) && item.sub_type == MI_LARGE_ROCK)
            return (true);
        if ((types & FIRE_NET) && item.sub_type == MI_THROWING_NET)
            return (true);

        if (types & FIRE_LAUNCHER)
        {
            if (launcher && item.launched_by(*launcher))
                return (true);
        }
    }
    else if (item.base_type == OBJ_WEAPONS
             && is_throwable(item, you.body_size()))
    {
        if ((types & FIRE_RETURNING)
            && item.special == SPWPN_RETURNING
            && item_ident(item, ISFLAG_KNOW_TYPE))
        {
            return (true);
        }
        if ((types & FIRE_DAGGER) && item.sub_type == WPN_DAGGER)
            return (true);
        if ((types & FIRE_SPEAR) && item.sub_type == WPN_SPEAR)
            return (true);
        if ((types & FIRE_HAND_AXE) && item.sub_type == WPN_HAND_AXE)
            return (true);
        if ((types & FIRE_CLUB) && item.sub_type == WPN_CLUB)
            return (true);
    }
    return (false);
}

// Returns inv slot that contains an item that looks like item,
// or -1 if not in inv.
static int _get_pack_slot(const item_def& item)
{
    if (! is_valid_item(item))
        return -1;

    // First try to find the exact same item.
    for (int i = 0; i < ENDOFPACK; i++)
    {
        const item_def& inv_item = you.inv[i];
        if (inv_item.quantity && _items_similar(item, you.inv[i], false))
            return i;
    }

    // If that fails, try to find an item sufficiently similar.
    for (int i = 0; i < ENDOFPACK; i++)
    {
        const item_def& inv_item = you.inv[i];
        if (inv_item.quantity && _items_similar(item, you.inv[i], true))
            return i;
    }

    return -1;
}

// Returns the type of ammo used by the player's equipped weapon,
// or AMMO_THROW if it's not a launcher.
static ammo_t _get_weapon_ammo_type(const item_def* weapon)
{
    if (weapon == NULL)
        return AMMO_THROW;
    if (weapon->base_type != OBJ_WEAPONS)
        return AMMO_THROW;

    switch (weapon->sub_type)
    {
        case WPN_BLOWGUN:
            return AMMO_BLOWGUN;
        case WPN_SLING:
            return AMMO_SLING;
        case WPN_BOW:
        case WPN_LONGBOW:
            return AMMO_BOW;
        case WPN_CROSSBOW:
            return AMMO_CROSSBOW;
        case WPN_HAND_CROSSBOW:
            return AMMO_HAND_CROSSBOW;
        default:
            return AMMO_THROW;
    }
}

static bool _items_similar(const item_def& a, const item_def& b, bool force)
{
    if (!force)
        return (items_similar(a, b) && a.slot == b.slot);

    // This is a reasonable implementation for now.
    return items_stack(a, b, force);
}
