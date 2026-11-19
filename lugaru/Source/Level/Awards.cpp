/*
Copyright (C) 2010-2017 - Lugaru contributors (see AUTHORS file)

This file is part of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

Lugaru is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Lugaru.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Level/Awards.hpp"

#include "Game.hpp"
#include "Objects/Person.hpp"
#include "EX/ExNet.hpp"
#include "EX/ExGen.hpp"
#include "Tutorial.hpp"

int bonus;
int bonusvalue;
int bonustotal;
int startbonustotal;
float bonustime;
float bonusnum[100];

const char* bonus_names[bonus_count] = {
#define DECLARE_BONUS(id, name, ...) name,
#include "Bonuses.def"
#undef DECLARE_BONUS
};

const char* award_names[award_count] = {
#define DECLARE_AWARD(id, name) name,
#include "Awards.def"
#undef DECLARE_AWARD
};

static const int bonus_values[bonus_count] = {
#define DECLARE_BONUS(id, name, value) value,
#include "Bonuses.def"
#undef DECLARE_BONUS
};

void award_bonus(int playerid, int bonusid, int alt_value)
{
    // LugaruEX co-op: every local/online player earns bonuses. The score is
    // shared (the global bonus feeds the total); the pop-up is per player.
    if (playerid < 0 || playerid >= (int)Person::players.size() ||
        (playerid != 0 && !Person::players[playerid]->isPlayerControlled())) {
        return;
    }
    bonus = bonusid;
    bonustime = 0;
    bonusvalue = alt_value ? alt_value : bonus_values[bonusid];
    Person& p = *Person::players[playerid];
    printf("LugaruEX: bonus %s +%d to person %d\n", bonus_names[bonusid], bonusvalue, playerid);
    p.exPopBonus = bonusid;
    p.exPopValue = bonusvalue;
    p.exPopTime = 0;
    EX::GenScore(playerid, bonusvalue); // PvP (Extras): every player keeps their own score
    EX::NetBonusEvent(playerid, bonusid, bonusvalue); // online: the pop-up on that player's own screen
    Tutorial::OnBonus(playerid, bonusid); // co-op tutorial: the cannon / spine crusher steps are per player
}

// FIXME: make these per-player
float damagetaken;
int numfalls;
int numflipfail;
int numseen;
int numresponded;
int numstaffattack;
int numswordattack;
int numknifeattack;
int numunarmedattack;
int numescaped;
int numflipped;
int numwallflipped;
int numthrowkill;
int numafterkill;
int numreversals;
int numattacks;
int maxalarmed;

int award_awards(int* awards)
{
    int numawards = 0;
    // LugaruEX co-op: the players are a team - partners are not counted as enemies
    unsigned enemies = 0;
    float teamBloodloss = 0;
    for (auto& p : Person::players) {
        if (p->isPlayerControlled()) {
            teamBloodloss += p->bloodloss;
        } else {
            enemies++;
        }
    }
    if (damagetaken == 0 && teamBloodloss == 0) {
        awards[numawards] = awardflawless;
        numawards++;
    }
    bool alldead = true;
    for (unsigned i = 1; i < Person::players.size(); i++) {
        if (Person::players[i]->dead != 2 && !Person::players[i]->isPlayerControlled()) {
            alldead = 0;
        }
    }
    if (alldead) {
        awards[numawards] = awardalldead;
        numawards++;
    }
    alldead = 1;
    for (unsigned i = 1; i < Person::players.size(); i++) {
        if (Person::players[i]->dead != 1 && !Person::players[i]->isPlayerControlled()) {
            alldead = 0;
        }
    }
    if (alldead) {
        awards[numawards] = awardnodead;
        numawards++;
    }
    if (numresponded == 0 && !numthrowkill) {
        awards[numawards] = awardstealth;
        numawards++;
    }
    if (numattacks == numstaffattack && numattacks > 0) {
        awards[numawards] = awardbojutsu;
        numawards++;
    }
    if (numattacks == numswordattack && numattacks > 0) {
        awards[numawards] = awardswordsman;
        numawards++;
    }
    if (numattacks == numknifeattack && numattacks > 0) {
        awards[numawards] = awardknifefighter;
        numawards++;
    }
    if (numattacks == numunarmedattack && numthrowkill == 0 && weapons.size() > 0) {
        awards[numawards] = awardkungfu;
        numawards++;
    }
    if (numescaped > 0) {
        awards[numawards] = awardevasion;
        numawards++;
    }
    if (numflipfail == 0 && numflipped + numwallflipped * 2 > 20) {
        awards[numawards] = awardacrobat;
        numawards++;
    }
    if (numthrowkill == (int)enemies) {
        awards[numawards] = awardlongrange;
        numawards++;
    }
    alldead = 1;
    for (unsigned i = 1; i < Person::players.size(); i++) {
        if (Person::players[i]->dead != 2 && !Person::players[i]->isPlayerControlled()) {
            alldead = 0;
        }
    }
    if (numafterkill > 0 && alldead) {
        awards[numawards] = awardbrutal;
        numawards++;
    }
    if (numreversals > ((float)numattacks) * .8 && numreversals > 3) {
        awards[numawards] = awardaikido;
        numawards++;
    }
    if (maxalarmed == 1 && enemies > 1) {
        awards[numawards] = awardstrategy;
        numawards++;
    }
    if (numflipfail > 3) {
        awards[numawards] = awardklutz;
        numawards++;
    }
    return numawards;
}

// LugaruEX PvP (Extras): the awards of one player, with the other players as the opponents.
// The round's awards (Flawless, Take no prisoners, Merciful, Brutal, the fast ones) go to the
// winner; the fighting-style ones to anyone by their own counts. No stealth / escape / divide
// and conquer: those are about enemies noticing you.
int award_awards_for(int person, int* awards)
{
    int numawards = 0;
    if (person < 0 || person >= (int)Person::players.size()) {
        return 0;
    }
    const Person& p = *Person::players[person];
    const Person::ExStats& st = p.exStats;
    const bool winner = EX::GenWinner() == person;
    int opponents = 0, killed = 0, knocked = 0;
    for (int i = 0; i < (int)Person::players.size(); i++) {
        const Person& o = *Person::players[i];
        if (i == person || !o.isPlayerControlled()) {
            continue;
        }
        opponents++;
        killed += o.dead == 2 ? 1 : 0;
        knocked += o.dead == 1 ? 1 : 0;
    }
    auto add = [&](int a) {
        if (numawards < award_count) {
            awards[numawards++] = a;
        }
    };
    if (winner && opponents > 0) {
        if (st.damage == 0 && p.bloodloss == 0) {
            add(awardflawless);
        }
        if (killed == opponents) {
            add(awardalldead);
        }
        if (knocked == opponents) {
            add(awardnodead);
        }
        if (st.afterkill > 0 && killed == opponents) {
            add(awardbrutal);
        }
        const float t = Game::wonleveltime;
        if (t > 0 && t < 60) {
            add(t < 25 ? awarddamnfast : t < 40 ? awardrealfast : awardfast);
        }
    }
    if (st.attacks > 0) {
        if (st.attacks == st.staves) {
            add(awardbojutsu);
        }
        if (st.attacks == st.swords) {
            add(awardswordsman);
        }
        if (st.attacks == st.knives) {
            add(awardknifefighter);
        }
        if (st.attacks == st.unarmed && st.throwhits == 0 && weapons.size() > 0) {
            add(awardkungfu);
        }
    }
    if (st.flipfail == 0 && st.flipped + st.wallflipped * 2 > 12) {
        add(awardacrobat);
    }
    if (opponents > 0 && st.throwhits >= opponents) {
        add(awardlongrange);
    }
    if (st.reversals > st.attacks * .8f && st.reversals > 2) {
        add(awardaikido);
    }
    if (st.flipfail > 3) {
        add(awardklutz);
    }
    return numawards;
}
