/*
Copyright (C) 2003, 2010 - Wolfire Games
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

#ifndef _PERSON_HPP_
#define _PERSON_HPP_

#include "Animation/Animation.hpp"
#include "Animation/Skeleton.hpp"
#include "Environment/Terrain.hpp"
#include "Graphic/Models.hpp"
#include "Graphic/Sprite.hpp"
#include "Graphic/gamegl.hpp"
#include "Math/XYZ.hpp"
#include "Objects/PersonType.hpp"
#include "Objects/Weapons.hpp"

#include <cmath>
#include <memory>
#include <string>

#define passivetype 0
#define guardtype 1
#define searchtype 2
#define attacktype 3
#define attacktypecutoff 4
#define playercontrolled 5
#define gethelptype 6
#define getweapontype 7
#define pathfindtype 8

struct InvalidPersonException : public exception
{
    const char* what() const throw()
    {
        return "Invalid weapon number";
    }
};

class Person : public enable_shared_from_this<Person>
{
private:
    float proportions[4];

public:
    static std::vector<std::shared_ptr<Person>> players;
    static void clearVictims();

    int whichpatchx;
    int whichpatchz;

    // animCurrent and animTarget are used to interpolate between different animations
    // (and for a bunch of other things).
    // animations interpolate with one another at various speeds.
    // animTarget seems to determine the combat state?
    int animCurrent;
    int animTarget;

    // frameCurrent and frameTarget are used to interpolate between the frames of an animation
    // (e.g. the crouched animation has only two frames, lerped back and forth slowly).
    // animations advance at various speeds.
    int frameCurrent;
    int frameTarget;

    int oldanimCurrent;
    int oldanimTarget;
    int oldframeCurrent;
    int oldframeTarget;

    int howactive;

    float parriedrecently;

    bool superruntoggle;

    int lastattack, lastattack2, lastattack3;

    XYZ currentoffset, targetoffset, offset;
    float target;
    float transspeed;

    XYZ realoldcoords;
    XYZ oldcoords;
    XYZ coords;
    XYZ velocity;

    float unconscioustime;

    bool immobile;

    float velspeed;
    float targetyaw;
    float targetrot;
    float rot;
    float oldrot;
    float lookyaw;
    float lookpitch;
    float yaw;
    float pitch;
    float lowyaw;
    float tilt;
    float targettilt;
    float tilt2;
    float targettilt2;
    bool rabbitkickenabled;

    float bloodloss;
    float bleeddelay;
    float skiddelay;
    float skiddingdelay;
    float deathbleeding;
    float tempdeltav;

    float damagetolerance;
    float damage;
    float permanentdamage;
    float superpermanentdamage;
    float lastcollide;
    /* Seems to be 0 = alive, 1 = unconscious, 2 = dead */
    int dead;

    float jumppower;
    bool onground;

    int wentforweapon;

    bool calcrot;

    XYZ facing;

    float bleeding;
    float bleedx, bleedy;
    int direction;
    float texupdatedelay;

    float headyaw, headpitch;
    float targetheadyaw, targetheadpitch;

    bool onterrain;
    bool pause;

    float grabdelay;

    std::shared_ptr<Person> victim;
    bool hasvictim;

    float updatedelay;
    float normalsupdatedelay;

    bool jumpstart;

    bool forwardkeydown;
    bool forwardstogglekeydown;
    bool rightkeydown;
    bool leftkeydown;
    bool backkeydown;
    bool jumpkeydown;
    bool jumptogglekeydown;
    bool crouchkeydown;
    bool crouchtogglekeydown;
    bool drawkeydown;
    bool drawtogglekeydown;
    bool throwkeydown;
    bool throwtogglekeydown;
    bool attackkeydown;
    bool feint;
    bool lastfeint;
    bool headless;

    float crouchkeydowntime;
    float jumpkeydowntime;
    bool freefall;

    float turnspeed;

    int aitype;
    float aiupdatedelay;
    float losupdatedelay;
    int ally;
    float collide;
    float collided;
    float avoidcollided;
    bool loaded;
    bool whichdirection;
    float whichdirectiondelay;
    bool avoidsomething;
    XYZ avoidwhere;
    float blooddimamount;

    float staggerdelay;
    float blinkdelay;
    float twitchdelay;
    float twitchdelay2;
    float twitchdelay3;
    float lefthandmorphness;
    float righthandmorphness;
    float headmorphness;
    float chestmorphness;
    float tailmorphness;
    float targetlefthandmorphness;
    float targetrighthandmorphness;
    float targetheadmorphness;
    float targetchestmorphness;
    float targettailmorphness;
    int lefthandmorphstart, lefthandmorphend;
    int righthandmorphstart, righthandmorphend;
    int headmorphstart, headmorphend;
    int chestmorphstart, chestmorphend;
    int tailmorphstart, tailmorphend;

    float weaponmissdelay;
    float highreversaldelay;
    float lowreversaldelay;

    int creature;

    unsigned id;

    Skeleton skeleton;

    float speed;
    float scale;
    float power;
    float speedmult;

    float protectionhead;
    float protectionhigh;
    float protectionlow;
    float armorhead;
    float armorhigh;
    float armorlow;
    bool metalhead;
    bool metalhigh;
    bool metallow;

    std::vector<std::string> clothes;
    std::vector<float> clothestintr;
    std::vector<float> clothestintg;
    std::vector<float> clothestintb;

    bool landhard;
    bool bled;
    bool spurt;
    bool onfire;
    float onfiredelay;
    float burnt;

    float flamedelay;

    int playerdetail;

    int num_weapons;
    int weaponids[4];
    /* Key of weaponids which is the weapon in hand, if any. -1 otherwise.
     * Always 0 or -1 as activeweapon is moved to position 0 when taken */
    int weaponactive;
    int weaponstuck;
    /* 0 or 1 to say if weapon is stuck in the front or the back  */
    int weaponstuckwhere;

    int numwaypoints;
    XYZ waypoints[90];
    int waypointtype[90];
    float pausetime;

    XYZ headtarget;
    float interestdelay;

    XYZ finalfinaltarget;
    XYZ finaltarget;
    int finalpathfindpoint;
    int targetpathfindpoint;
    int lastpathfindpoint;
    int lastpathfindpoint2;
    int lastpathfindpoint3;
    int lastpathfindpoint4;

    int waypoint;

    XYZ lastseen;
    float lastseentime;
    float lastchecktime;
    float stunned;
    float surprised;
    float runninghowlong;
    int occluded;
    int lastoccluded;
    int laststanding;
    int escapednum;

    float speechdelay;
    float neckspurtdelay;
    float neckspurtparticledelay;
    float neckspurtamount;

    int whichskin;
    bool rabbitkickragdoll;

    Animation tempanimation;

    bool jumpclimb;

    Person();
    Person(FILE*, int, unsigned);
    Person(Json::Value, int, unsigned);

    void skeletonLoad();

    // convenience functions
    inline Joint& joint(int bodypart) { return skeleton.joints[skeleton.jointlabels[bodypart]]; }
    inline XYZ& jointPos(int bodypart) { return joint(bodypart).position; }
    inline XYZ& jointVel(int bodypart) { return joint(bodypart).velocity; }
    inline AnimationFrame& currentFrame() { return Animation::exFrame(animCurrent, frameCurrent); }
    inline AnimationFrame& targetFrame() { return Animation::exFrame(animTarget, frameTarget); }

    void setProportions(float head, float body, float arms, float legs);
    float getProportion(int part) const;
    XYZ getProportionXYZ(int part) const;

    void changeCreatureType(person_type type);

    void CheckKick();
    void CatchFire();
    void DoBlood(float howmuch, int which);
    void DoBloodBig(float howmuch, int which);
    bool DoBloodBigWhere(float howmuch, int which, XYZ where);

    bool wasIdle() { return animation_bits[animCurrent] & ab_idle; }
    bool isIdle() { return animation_bits[animTarget] & ab_idle; }
    int getIdle();

    bool isSitting() { return animation_bits[animTarget] & ab_sit; }

    bool isSleeping() { return animation_bits[animTarget] & ab_sleep; }

    bool wasCrouch() { return animation_bits[animCurrent] & ab_crouch; }
    bool isCrouch() { return animation_bits[animTarget] & ab_crouch; }
    int getCrouch();

    bool wasStop() { return animation_bits[animCurrent] & ab_stop; }
    bool isStop() { return animation_bits[animTarget] & ab_stop; }
    int getStop();

    bool wasSneak();
    bool isSneak();
    int getSneak();

    bool wasRun() { return animation_bits[animCurrent] & ab_run; }
    bool isRun() { return animation_bits[animTarget] & ab_run; }
    int getRun();

    bool wasLanding() { return animation_bits[animCurrent] & ab_land; }
    bool isLanding() { return animation_bits[animTarget] & ab_land; }
    int getLanding();

    bool wasLandhard() { return animation_bits[animCurrent] & ab_landhard; }
    bool isLandhard() { return animation_bits[animTarget] & ab_landhard; }
    int getLandhard();

    bool wasFlip() { return animation_bits[animCurrent] & ab_flip; }
    bool isFlip() { return animation_bits[animTarget] & ab_flip; }

    bool isWallJump() { return animation_bits[animTarget] & ab_walljump; }
    void Reverse();
    void DoDamage(float howmuch);
    void exMorphs();     // LugaruEX: face / hand / tail morphs of DoStuff (online guests run them too)
    void exHeldToHand(int weapon, const XYZ& aim); // LugaruEX: before a throw, a weapon not drawn lately goes to the hand
    static int exBonusPlayerFor(const Person* p); // LugaruEX: the human a bonus caused by p goes to (single player: 0)
    void exAnimMorphs(); // LugaruEX: hands and mouth that follow the animation
    void exFireVisual(float mult); // LugaruEX online guest: flames and burning, shown only
    void exFireStart();            // LugaruEX online guest: CatchFire's burst of flames and the fire loop, shown only
    void DoHead();
    void DoMipmaps()
    {
        skeleton.drawmodel.textureptr.bind();
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, skeleton.skinsize, skeleton.skinsize, 0, GL_RGB, GL_UNSIGNED_BYTE, &skeleton.skinText[0]);
    }

    int SphereCheck(XYZ* p1, float radius, XYZ* p, XYZ* move, float* rotate, Model* model);
    int DrawSkeleton();
    void Puff(int whichlabel);
    void FootLand(bodypart whichfoot, float opacity);
    void DoStuff();
    void setTargetAnimation(int);
    void DoAnimations();
    void RagDoll(bool checkcollision);

    void takeWeapon(int weaponId);

    bool addClothes(const int& clothesId);
    void addClothes();

    void doAI();

    bool catchKnife();

    bool hasWeapon() { return (weaponactive != -1); }
    bool isPlayerControlled() { return (aitype == playercontrolled); }

    // LugaruEX local co-op: per-human attack-key state (player 1 keeps using
    // the game's globals) and, for AI, which human it is currently after
    bool exOldAttack = false;
    bool exRealAttack = false;
    bool exWalk = false; // a player asks to walk instead of run (Ctrl / a light stick push)
    // a body a player just hit (jump kick, throw...) never knocks that player over, nor they it,
    // while it flies (GameTick person collisions); compared only, never followed
    const Person* exSparedBy = nullptr;
    float exSparedUntil = 0;
    int exTarget = 0;
    // score pop-up in the middle of this player's own screen
    int exPopBonus = 0, exPopValue = 0;
    float exPopTime = 10;
    // skin texture set by the launcher's character choice ("" = the creature's own skins[whichskin])
    std::string exSkinPath;
    // custom model folder ("Models/Custom/rabbit/Name/"; "" = the creature's own files)
    std::string exModelDir;
    bool exPoseReady = false; // online guest: limb rotations computed at least once
    std::string exOutfit; // the launcher's Clothes choice ("" = the level's own)
    int exSkinState = -1; // free * 4 + dead when the mesh was last skinned
    // AI looks around only while 'occluded' (kept by this PC's camera when drawing) is low;
    // a player on another PC has their own camera, so for them the same test is run from
    // where they are (exSightOccluded)
    int exFarOccluded = 0, exFarLastOccluded = 0, exFarFor = -1;
    float exFarDelay = 0;
    int exSightOccluded(int target);
    // online guest: pose from the host's animation state (no game events); freshTemp
    // = just got up from a ragdoll, so the get-up animation starts from this pose
    void exNetPose(bool freshTemp);

    operator Json::Value();
};

const int maxplayers = 10;

#endif
