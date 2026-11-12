/*
Copyright (C) 2003, 2010 - Wolfire Games
Copyright (C) 2010-2017 - Lugaru contributors (see AUTHORS file)

This file is part of Lugaru.

Lugaru is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

Lugaru is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Lugaru.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _TUTORIAL_HPP_
#define _TUTORIAL_HPP_

class Tutorial
{
public:
    static bool active;
    static int stage;
    static float stagetime;
    static float maxtime;

    static void Do(float multiplier);
    static void DrawTextInfo();
    static void DoStuff(float multiplier);

    // LugaruEX co-op tutorial: every player is in it, and a step moves on once
    // each of them has done it.
    /** the tutorial's see-through sparring partner (anyone who isn't a player) */
    static bool IsGhost(int personId);
    /** players who finished the current step / players in the tutorial */
    static int PlayersDone();
    static int PlayersIn();
    /** has this player finished the current step (they wait for the others) */
    static bool DoneBy(int personId);
    /** online guests: the host's step progress (done count, player count) */
    static void SetRemoteProgress(int done, int total, unsigned int doneMask);
    /** host: which players (person index bits) are done with this step */
    static unsigned int DoneMask();
    /** a player earned a bonus (cannon / spine crusher steps) */
    static void OnBonus(int personId, int bonus);

private:
    static float success;
};

#endif
