/*
LugaruEX launcher - animated 3D character preview, the part both launchers share.

Loads the player model (.solid), skeleton and idle animation straight from the
user's Lugaru HD Data folder and skins it the same way the engine does
(Skeleton::Load rest-pose inverse + Person::DrawSkeleton muscle transforms),
then draws it with plain OpenGL into the current context. The Windows launcher
(preview.cpp) and the SDL launcher (launcher-sdl) give it a window and input.

Copyright (C) 2026 - LugaruEX contributors. Based on Lugaru (C) Wolfire Games
and the OSS Lugaru contributors. GPLv2 or later.
*/

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "preview_core.h"
#include "../shared/outfits.h"

#include "../lugaru/Source/EX/ExMath.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#ifndef _WIN32
#include <dirent.h>
#include <strings.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

namespace
{
// UTF-8 path -> FILE* (Windows needs the wide name for non-ASCII folders)
static FILE* openU8(const std::string& path)
{
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    std::wstring w(n > 0 ? n : 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n);
    return _wfopen(w.c_str(), L"rb");
#else
    return fopen(path.c_str(), "rb");
#endif
}
// ---------------------------------------------------------------- math
struct V3
{
    float x = 0, y = 0, z = 0;
    V3() {}
    V3(float a, float b, float c)
        : x(a)
        , y(b)
        , z(c)
    {
    }
    V3 operator+(const V3& o) const { return V3(x + o.x, y + o.y, z + o.z); }
    V3 operator-(const V3& o) const { return V3(x - o.x, y - o.y, z - o.z); }
    V3 operator*(float s) const { return V3(x * s, y * s, z * s); }
    V3 operator/(float s) const { return V3(x / s, y / s, z / s); }
};
static float len(const V3& v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
static V3 cross(const V3& p, const V3& q) { return V3(p.y * q.z - p.z * q.y, p.z * q.x - p.x * q.z, p.x * q.y - p.y * q.x); }
static void normalise(V3& v)
{
    float d = len(v);
    if (d != 0) {
        v = v / d;
    }
}
static float distf(const V3& a, const V3& b) { return len(a - b); }
static bool isnorm(float f) { return std::isnormal(f); }

// identical to Lugaru's DoRotation (degrees; y, then z, then x)
static V3 doRotation(V3 p, float xa, float ya, float za)
{
    const float k = 6.283185f / 360;
    xa *= k;
    ya *= k;
    za *= k;
    V3 n;
    if (ya) {
        n.z = p.z * cosf(ya) - p.x * sinf(ya);
        n.x = p.z * sinf(ya) + p.x * cosf(ya);
        p.z = n.z;
        p.x = n.x;
    }
    if (za) {
        n.x = p.x * cosf(za) - p.y * sinf(za);
        n.y = p.y * cosf(za) + p.x * sinf(za);
        p.x = n.x;
        p.y = n.y;
    }
    if (xa) {
        n.y = p.y * cosf(xa) - p.z * sinf(xa);
        n.z = p.y * sinf(xa) + p.z * cosf(xa);
        p.z = n.z;
        p.y = n.y;
    }
    return p;
}

// ---------------------------------------------------------------- big-endian reader
struct BE
{
    FILE* f = nullptr;
    bool ok = true;
    explicit BE(const std::string& path) { f = openU8(path); ok = (f != nullptr); }
    ~BE()
    {
        if (f) {
            fclose(f);
        }
    }
    bool raw(void* d, size_t n)
    {
        if (!ok || fread(d, 1, n, f) != n) {
            ok = false;
            memset(d, 0, n);
        }
        return ok;
    }
    unsigned char u8()
    {
        unsigned char c;
        raw(&c, 1);
        return c;
    }
    short i16()
    {
        unsigned char b[2];
        raw(b, 2);
        return (short)((b[0] << 8) | b[1]);
    }
    int i32()
    {
        unsigned char b[4];
        raw(b, 4);
        return (int)((unsigned)b[0] << 24 | (unsigned)b[1] << 16 | (unsigned)b[2] << 8 | b[3]);
    }
    float f32()
    {
        int i = i32();
        float r;
        memcpy(&r, &i, 4);
        return r;
    }
    // x, y, z in file order (V3(f32(), f32(), f32()) leaves the order to the compiler:
    // MSVC read z first, which mirrored every model, skeleton and animation)
    V3 v3()
    {
        const float x = f32();
        const float y = f32();
        const float z = f32();
        return V3(x, y, z);
    }
};

// joint labels (Animation/Joint.hpp bodypart)
enum
{
    head,
    neck,
    leftshoulder,
    leftelbow,
    leftwrist,
    lefthand,
    rightshoulder,
    rightelbow,
    rightwrist,
    righthand,
    abdomen,
    lefthip,
    righthip,
    groin,
    leftknee,
    leftankle,
    leftfoot,
    rightknee,
    rightankle,
    rightfoot,
    numlabels
};

struct Tri
{
    int v[3];
    float u[3], t[3];
};
struct Joint
{
    V3 rest;
    int label = 0;
    bool lower = false;
};
struct Muscle
{
    int p1 = 0, p2 = 0;
    std::vector<int> verts;
};

struct Rig
{
    bool ok = false;
    std::vector<V3> verts;  // model vertices (rest, scaled)
    std::vector<V3> local;  // muscle-local offsets
    std::vector<Tri> tris;
    std::vector<Joint> joints;
    std::vector<Muscle> muscles;
    int fwd[3] = { 0, 0, 0 }, lowfwd[3] = { 0, 0, 0 };
    int byLabel[numlabels];     // jointlabels while Skeleton::Load builds the rest pose
    int byLabelPose[numlabels]; // the filled table every drawn frame uses
    std::vector<std::vector<V3>> frames;
    std::vector<float> speeds;
    float prop[4] = { 1, 1, 1, 1 };
    // pose scratch
    std::vector<V3> posed, normals;
    std::vector<bool> owned;
};

struct Forwards
{
    V3 forward, lowforward, special[5];
};

static Forwards findForwards(const Rig& r, const std::vector<V3>& J, bool rest = false)
{
    Forwards f;
    const int* tab = rest ? r.byLabel : r.byLabelPose;
    auto jp = [&](int label) { return J[tab[label]]; };
    f.forward = cross(J[r.fwd[1]] - J[r.fwd[0]], J[r.fwd[2]] - J[r.fwd[0]]);
    normalise(f.forward);
    f.lowforward = cross(J[r.lowfwd[1]] - J[r.lowfwd[0]], J[r.lowfwd[2]] - J[r.lowfwd[0]]);
    normalise(f.lowforward);
    f.special[0] = f.forward;
    f.special[1] = jp(rightelbow) - (jp(rightshoulder) + jp(rightwrist)) / 2 + f.forward * .4f;
    normalise(f.special[1]);
    f.special[2] = jp(leftelbow) - (jp(leftshoulder) + jp(leftwrist)) / 2 + f.forward * .4f;
    normalise(f.special[2]);
    f.special[3] = (jp(righthip) + jp(rightankle)) / 2 - jp(rightknee) + f.lowforward * .4f;
    normalise(f.special[3]);
    f.special[4] = (jp(lefthip) + jp(leftankle)) / 2 - jp(leftknee) + f.lowforward * .4f;
    normalise(f.special[4]);
    return f;
}

// identical to Skeleton::FindRotationMuscle (skeleton not ragdolled, not hanging)
static void findRotation(const Rig& r, const std::vector<V3>& J, const Forwards& F, int m, float& r1, float& r2, float& r3)
{
    const Muscle& mu = r.muscles[m];
    V3 p1 = J[mu.p1], p2 = J[mu.p2], fwd;
    r1 = r2 = r3 = 0;
    float dist = distf(p1, p2);
    if (p1.y - p2.y <= dist) {
        r2 = asinf((p1.y - p2.y) / dist);
    }
    if (p1.y - p2.y > dist) {
        r2 = asinf(1.f);
    }
    r2 *= 360.0f / 6.2831853f;
    p1.y = 0;
    p2.y = 0;
    dist = distf(p1, p2);
    if (p1.z - p2.z <= dist) {
        r1 = acosf((p1.z - p2.z) / dist);
    }
    if (p1.z - p2.z > dist) {
        r1 = acosf(1.f);
    }
    r1 *= 360.0f / 6.2831853f;
    if (p1.x > p2.x) {
        r1 = 360 - r1;
    }
    if (!isnorm(r1)) {
        r1 = 0;
    }
    if (!isnorm(r2)) {
        r2 = 0;
    }
    const int l1 = r.joints[mu.p1].label, l2 = r.joints[mu.p2].label;
    switch (l1) {
        case head:
            fwd = F.special[0];
            break;
        case rightshoulder:
        case rightelbow:
        case rightwrist:
        case righthand:
            fwd = F.special[1];
            break;
        case leftshoulder:
        case leftelbow:
        case leftwrist:
        case lefthand:
            fwd = F.special[2];
            break;
        case righthip:
        case rightknee:
        case rightankle:
        case rightfoot:
            fwd = F.special[3];
            break;
        case lefthip:
        case leftknee:
        case leftankle:
        case leftfoot:
            fwd = F.special[4];
            break;
        default:
            fwd = r.joints[mu.p1].lower ? F.lowforward : F.forward;
            break;
    }
    if (l1 == rightfoot || l2 == rightfoot || l1 == leftfoot || l2 == leftfoot) {
        fwd.y -= .3f;
    }
    fwd = doRotation(fwd, 0, r1 - 90, 0);
    fwd = doRotation(fwd, 0, 0, r2 - 90);
    fwd.y = 0;
    fwd = fwd / len(fwd);
    if (fwd.z <= 1 && fwd.z >= -1) {
        r3 = acosf(0 - fwd.z);
    } else {
        r3 = acosf(-1.f);
    }
    r3 *= 360.0f / 6.2831853f;
    if (0 > fwd.x) {
        r3 = 360 - r3;
    }
    if (!isnorm(r3)) {
        r3 = 0;
    }
}

static std::string firstExisting(const std::string& dir, std::initializer_list<const char*> names)
{
    for (auto n : names) {
        const std::string p = dir + n;
        if (FILE* f = openU8(p)) {
            fclose(f);
            return p;
        }
    }
#ifndef _WIN32
    // Linux: file names are case-sensitive, the game's are not
    if (DIR* d = opendir(dir.c_str())) {
        std::string found;
        while (dirent* e = readdir(d)) {
            for (auto n : names) {
                if (found.empty() && strcasecmp(e->d_name, n) == 0) {
                    found = dir + e->d_name;
                }
            }
        }
        closedir(d);
        return found;
    }
#endif
    return "";
}

// the joint label of every joint in a skeleton file (Joint::load)
static std::vector<int> skeletonLabels(const std::string& path)
{
    std::vector<int> out;
    BE b(path);
    const int nj = b.i32();
    if (!b.ok || nj <= 0 || nj > 100) {
        return out;
    }
    for (int i = 0; i < nj && b.ok; i++) {
        for (int k = 0; k < 5; k++) {
            b.f32(); // position, length, mass
        }
        b.u8();
        b.u8();
        b.i32();
        b.u8();
        b.u8();
        out.push_back(b.i32()); // label
        b.i32();
        b.u8();
        b.i32();
    }
    if (!b.ok) {
        out.clear();
    }
    return out;
}

static bool loadRig(Rig& r, const std::string& data, bool wolf)
{
    r = Rig();
    const std::string model = firstExisting(data + "Models/", { wolf ? "Wolf.solid" : "Body.solid" });
    const std::string skel = wolf ? firstExisting(data + "Skeleton/", { "Basic Figure wolf", "BasicFigureWolf" })
                                   : firstExisting(data + "Skeleton/", { "Basic Figure", "BasicFigure" });
    const std::string anim = firstExisting(data + "Animations/", { wolf ? "Wolfidle" : "Idle" });
    if (model.empty() || skel.empty() || anim.empty()) {
        return false;
    }
    if (wolf) {
        r.prop[0] = r.prop[1] = r.prop[2] = r.prop[3] = 1.1f;
    } else {
        r.prop[0] = 1.2f;
        r.prop[1] = 1.05f;
        r.prop[2] = 1.f;
        r.prop[3] = 1.1f;
    }

    { // .solid model (Model::load + Rotate(180,0,0) + Scale(.04) + FlipTexCoords)
        BE b(model);
        const int vn = (unsigned short)b.i16(), tn = (unsigned short)b.i16();
        if (!b.ok || vn <= 0 || tn <= 0) {
            return false;
        }
        r.verts.resize(vn);
        for (int i = 0; i < vn; i++) {
            V3 v = b.v3();
            r.verts[i] = doRotation(v, 180, 0, 0) * .04f;
        }
        r.tris.resize(tn);
        for (int i = 0; i < tn; i++) {
            short s[6];
            for (short& x : s) {
                x = b.i16();
            }
            Tri& t = r.tris[i];
            t.v[0] = (unsigned short)s[0];
            t.v[1] = (unsigned short)s[2];
            t.v[2] = (unsigned short)s[4];
            for (float& x : t.u) {
                x = b.f32();
            }
            for (float& x : t.t) {
                x = -b.f32();
            }
        }
        if (!b.ok) {
            return false;
        }
    }
    const int vn = (int)r.verts.size();

    { // skeleton (Joint::load, Muscle::load)
        BE b(skel);
        const int nj = b.i32();
        if (!b.ok || nj <= 0 || nj > 100) {
            return false;
        }
        r.joints.resize(nj);
        for (auto& j : r.joints) {
            j.rest = b.v3();
            b.f32();
            b.f32();     // length, mass
            b.u8();
            b.u8();      // hasparent, locked
            b.i32();     // modelnum
            b.u8();
            b.u8();      // visible, sametwist
            j.label = b.i32();
            b.i32();     // hasgun
            j.lower = b.u8() != 0;
            b.i32();     // parent
        }
        const int nm = b.i32();
        if (!b.ok || nm <= 0 || nm > 200) {
            return false;
        }
        r.muscles.resize(nm);
        for (auto& m : r.muscles) {
            for (int k = 0; k < 5; k++) {
                b.f32();
            }
            b.i32(); // type
            const int n = b.i32();
            if (n < 0 || n > 100000) {
                return false;
            }
            for (int k = 0; k < n; k++) {
                int v = b.i32();
                if (v >= 0 && v < vn) {
                    m.verts.push_back(v);
                }
            }
            b.u8(); // visible
            m.p1 = b.i32();
            m.p2 = b.i32();
            if (m.p1 < 0 || m.p1 >= nj || m.p2 < 0 || m.p2 >= nj) {
                return false;
            }
        }
        for (int& x : r.fwd) {
            x = b.i32();
        }
        for (int& x : r.lowfwd) {
            x = b.i32();
        }
        if (!b.ok) {
            return false;
        }
    }
    // Every drawn frame finds the limbs' "special forward" vectors through the
    // skeleton's filled jointlabels table. Skeleton::Load builds the rest pose
    // *before* it fills that table: on a fresh skeleton (a rabbit player as the
    // level loads it) it is still all zero, so those vectors degenerate to
    // forward/lowforward there. A wolf player is made by changeCreatureType,
    // which reloads a skeleton whose table the rabbit already filled.
    for (int& x : r.byLabel) {
        x = 0;
    }
    for (int& x : r.byLabelPose) {
        x = 0;
    }
    for (int i = 0; i < (int)r.joints.size(); i++) {
        if (r.joints[i].label >= 0 && r.joints[i].label < numlabels) {
            r.byLabelPose[r.joints[i].label] = i;
        }
    }
    if (wolf) {
        const std::vector<int> rl = skeletonLabels(firstExisting(data + "Skeleton/", { "Basic Figure", "BasicFigure" }));
        for (int i = 0; i < (int)rl.size(); i++) {
            if (rl[i] >= 0 && rl[i] < numlabels && i < (int)r.joints.size()) {
                r.byLabel[rl[i]] = i;
            }
        }
    }

    { // idle animation: joint positions + frame speed
        BE b(anim);
        const int nf = b.i32(), nj = b.i32();
        if (!b.ok || nf <= 0 || nf > 1000 || nj != (int)r.joints.size()) {
            return false;
        }
        r.frames.resize(nf);
        r.speeds.resize(nf);
        for (int f = 0; f < nf; f++) {
            r.frames[f].resize(nj);
            for (int j = 0; j < nj; j++) {
                r.frames[f][j] = b.v3();
            }
            for (int j = 0; j < nj; j++) {
                b.f32(); // twist
            }
            for (int j = 0; j < nj; j++) {
                b.u8(); // onground
            }
            r.speeds[f] = b.f32();
        }
        if (!b.ok) {
            return false;
        }
    }

    // rest-pose inverse (Skeleton::Load): vertex -> muscle-local offset
    std::vector<V3> rest(r.joints.size());
    for (size_t i = 0; i < rest.size(); i++) {
        rest[i] = r.joints[i].rest;
    }
    Forwards F = findForwards(r, rest, true);
    std::vector<int> owner(vn, -1);
    for (int m = 0; m < (int)r.muscles.size(); m++) {
        for (int v : r.muscles[m].verts) {
            owner[v] = m;
        }
    }
    r.local.assign(vn, V3());
    r.owned.assign(vn, false);
    for (int m = 0; m < (int)r.muscles.size(); m++) {
        float r1, r2, r3;
        findRotation(r, rest, F, m, r1, r2, r3);
        EX::Mat4 M;
        M.rotate(r3, 0, 1, 0);
        M.rotate(r2 - 90, 0, 0, 1);
        M.rotate(r1 - 90, 0, 1, 0);
        const V3 mid = (rest[r.muscles[m].p1] + rest[r.muscles[m].p2]) / 2;
        for (int v = 0; v < vn; v++) {
            if (owner[v] != m) {
                continue;
            }
            V3 d = r.verts[v] - mid;
            M.point(d.x, d.y, d.z, r.local[v].x, r.local[v].y, r.local[v].z);
        }
    }
    r.posed.assign(vn, V3());
    r.normals.assign(vn, V3());
    r.ok = true;
    return true;
}

// Person::DrawSkeleton vertex pass for the given joint positions
static void pose(Rig& r, const std::vector<V3>& J)
{
    Forwards F = findForwards(r, J);
    std::fill(r.owned.begin(), r.owned.end(), false);
    for (int m = 0; m < (int)r.muscles.size(); m++) {
        const Muscle& mu = r.muscles[m];
        if (mu.verts.empty()) {
            continue;
        }
        float r1, r2, r3;
        findRotation(r, J, F, m, r1, r2, r3);
        const V3 mid = (J[mu.p1] + J[mu.p2]) / 2;
        EX::Mat4 M;
        M.translate(mid.x, mid.y, mid.z);
        M.rotate(-r1 + 90, 0, 1, 0);
        M.rotate(-r2 + 90, 0, 0, 1);
        M.rotate(-r3, 0, 1, 0);
        const int a = r.joints[mu.p1].label, b = r.joints[mu.p2].label;
        const bool cAbd = (a == abdomen || b == abdomen);
        const bool cArm = (a == lefthand || a == righthand || a == leftwrist || a == rightwrist || a == leftelbow || a == rightelbow || b == leftelbow || b == rightelbow);
        const bool cLeg = (a == leftfoot || a == rightfoot || a == leftankle || a == rightankle || a == leftknee || a == rightknee || b == leftknee || b == rightknee);
        const bool cHead = (a == head || b == head);
        for (int v : mu.verts) {
            const V3& l = r.local[v];
            V3 o;
            auto add = [&](float p) { o = o + l * p; };
            if (cAbd) {
                add(r.prop[1]);
            }
            if (cArm) {
                add(r.prop[2]);
            }
            if (cLeg) {
                add(r.prop[3]);
            }
            if (cHead) {
                add(r.prop[0]);
            }
            M.point(o.x, o.y, o.z, r.posed[v].x, r.posed[v].y, r.posed[v].z);
            r.owned[v] = true;
        }
    }
    // smooth normals (triangles are clockwise seen from outside, like the engine)
    std::fill(r.normals.begin(), r.normals.end(), V3());
    for (const Tri& t : r.tris) {
        V3 n = cross(r.posed[t.v[2]] - r.posed[t.v[0]], r.posed[t.v[1]] - r.posed[t.v[0]]);
        for (int k = 0; k < 3; k++) {
            r.normals[t.v[k]] = r.normals[t.v[k]] + n;
        }
    }
    for (V3& n : r.normals) {
        normalise(n);
    }
}

// ---------------------------------------------------------------- view state
static const float kAnimSpeed = 0.175f; // idle plays at 17.5% of game speed
static const float kReturnTime = 2.5f;  // seconds to glide back to the front view
static const float kFloatFrom = 0.0f;   // rabbit float: frame range (fractional)
static const float kFloatTo = 1.25f;
static const float kFloatPeriod = 4.0f; // seconds for one down-and-up float
static const float kWolfFloatFrom = 0.0f;
static const float kWolfFloatTo = 3.0f;
static const float kWolfFloatPeriod = 5.0f;
static const float kDefaultYaw = 0.f;    // front view
static const float kDefaultPitch = 8.f;

struct State
{
    std::string dataDir;
    Rig rig;
    std::string model;
    bool dimmed = false;
    GLuint tex = 0;
    int texW = 0, texH = 0;
    // animation
    int frame = 0;
    float t = 0;
    // view
    float yaw = kDefaultYaw, pitch = 8, zoom = 1; // facing the camera
    bool returning = false;
    float phase = 0; // rabbit float oscillator
    float returnT = 0, fromYaw = 0, fromPitch = 0, fromZoom = 1, dYaw = 0, dPitch = 0;
    bool dragging = false;
    int dragX = 0, dragY = 0;
    unsigned lastInput = 0;
    float bbMin[3] = { 0, 0, 0 }, bbMax[3] = { 0, 0, 0 };
};
static State g;

static void currentJoints(std::vector<V3>& J)
{
    const Rig& r = g.rig;
    const int nf = (int)r.frames.size();
    const int a = g.frame % nf, b = (g.frame + 1) % nf;
    J.resize(r.joints.size());
    for (size_t j = 0; j < J.size(); j++) {
        J[j] = r.frames[a][j] * (1 - g.t) + r.frames[b][j] * g.t;
    }
    static const bool restDbg = getenv("LUGARUEX_PREVIEW_REST") != nullptr; // debug: bind pose
    if (restDbg) {
        for (size_t j = 0; j < J.size(); j++) {
            J[j] = r.joints[j].rest;
        }
    }
}

static void computeBounds()
{
    // bounds over the whole idle loop, so the camera never has to move
    float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
    const int keepF = g.frame;
    const float keepT = g.t;
    for (int f = 0; f < (int)g.rig.frames.size(); f++) {
        g.frame = f;
        g.t = 0;
        std::vector<V3> J;
        currentJoints(J);
        pose(g.rig, J);
        for (size_t i = 0; i < g.rig.posed.size(); i++) {
            if (!g.rig.owned[i]) {
                continue;
            }
            const float* p = &g.rig.posed[i].x;
            for (int k = 0; k < 3; k++) {
                mn[k] = std::min(mn[k], p[k]);
                mx[k] = std::max(mx[k], p[k]);
            }
        }
    }
    g.frame = keepF;
    g.t = keepT;
    memcpy(g.bbMin, mn, sizeof(mn));
    memcpy(g.bbMax, mx, sizeof(mx));
}
} // namespace

void PreviewCore_SetDataDir(const std::string& dataDir)
{
    g.dataDir = dataDir;
}

void PreviewCore_SetModel(const std::string& model)
{
    if (model == g.model) {
        return;
    }
    g.model = model;
    loadRig(g.rig, g.dataDir, model == "wolf");
    g.frame = 0;
    g.t = 0;
    if (g.rig.ok) {
        computeBounds();
    }
}

void PreviewCore_SetTexture(const unsigned char* rgba, int w, int h)
{
    if (g.tex) {
        glDeleteTextures(1, &g.tex);
        g.tex = 0;
    }
    if (!rgba || w <= 0 || h <= 0) {
        return;
    }
    // the engine stores images bottom-up; do the same so its texcoords match
    std::vector<unsigned char> flip((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        memcpy(&flip[(size_t)y * w * 4], &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    }
    glGenTextures(1, &g.tex);
    glBindTexture(GL_TEXTURE_2D, g.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flip.data());
    g.texW = w;
    g.texH = h;
}

std::vector<PreviewCloth> PreviewCore_Outfit(const std::string& key)
{
    // the same outfits the game puts on (shared/outfits.h)
    std::vector<PreviewCloth> out;
    for (const OutfitDef& o : Outfits()) {
        if (key == o.key) {
            for (const OutfitPiece& p : o.pieces) {
                out.push_back({ p.tex, p.r, p.g, p.b });
            }
        }
    }
    return out;
}

void PreviewCore_PaintCloth(std::vector<unsigned char>& skin, int w, int h, const unsigned char* cloth, int cw, int chh, float r, float g, float b)
{
    if (!cloth || cw <= 0 || chh <= 0 || w <= 0 || h <= 0 || skin.size() < (size_t)w * h * 4) {
        return;
    }
    const float tint[3] = { std::max(0.f, std::min(1.f, r)), std::max(0.f, std::min(1.f, g)), std::max(0.f, std::min(1.f, b)) };
    for (int y = 0; y < h; y++) {
        const int sy = std::min(chh - 1, (int)((y + 0.5f) * chh / h));
        for (int x = 0; x < w; x++) {
            const int sx = std::min(cw - 1, (int)((x + 0.5f) * cw / w));
            const unsigned char* c = cloth + ((size_t)sy * cw + sx) * 4;
            unsigned char* d = &skin[((size_t)y * w + x) * 4];
            const float a = c[3] / 255.f;
            for (int k = 0; k < 3; k++) {
                d[k] = (unsigned char)(d[k] * (1 - a) + c[k] * tint[k] * a + 0.5f);
            }
        }
    }
}

void PreviewCore_SetDimmed(bool dimmed)
{
    g.dimmed = dimmed;
}

void PreviewCore_FreeGL()
{
    if (g.tex) {
        glDeleteTextures(1, &g.tex);
        g.tex = 0;
    }
}

void PreviewCore_DragBegin(int x, int y, unsigned nowMs)
{
    g.dragging = true;
    g.dragX = x;
    g.dragY = y;
    g.lastInput = nowMs;
}

void PreviewCore_DragMove(int x, int y, unsigned nowMs)
{
    if (!g.dragging) {
        return;
    }
    g.yaw += (x - g.dragX) * 0.6f;
    g.pitch = fmodf(g.pitch + (y - g.dragY) * 0.6f, 360.f); // full rotation, any direction
    g.dragX = x;
    g.dragY = y;
    g.lastInput = nowMs;
}

void PreviewCore_DragEnd(unsigned nowMs)
{
    g.dragging = false;
    g.lastInput = nowMs;
}

bool PreviewCore_Dragging()
{
    return g.dragging;
}

void PreviewCore_Wheel(int dir, unsigned nowMs)
{
    g.zoom = std::max(0.7f, std::min(2.5f, g.zoom * (dir > 0 ? 1.1f : 1 / 1.1f)));
    g.lastInput = nowMs;
}

void PreviewCore_Turn(float dYaw, float dPitch, unsigned nowMs)
{
    g.yaw += dYaw;
    g.pitch = fmodf(g.pitch + dPitch, 360.f);
    g.lastInput = nowMs;
}

void PreviewCore_Advance(double dt, unsigned nowMs)
{
    if (dt > 0.1) {
        dt = 0.1;
    }
    if (g.rig.ok) {
        const int nf = (int)g.rig.frames.size();
        const float s = std::max(g.rig.speeds[g.frame % nf], g.rig.speeds[(g.frame + 1) % nf]);
        // engine rate is target += multiplier * speed * 2; the preview plays it
        // at a slow, calm pace so the pose is easy to look at
        const bool wolf = (g.model == "wolf");
        if (nf >= 4) {
            // Float smoothly back and forth over the clean part of the idle.
            // Rabbit: at the bottom of its bob (frame 2) one foot twists and
            // points down. Wolf: around frames 4.5-6 an arm twists straight.
            // (In game, foot IK and the other animations hide these.)
            const float from = wolf ? kWolfFloatFrom : kFloatFrom, to = wolf ? kWolfFloatTo : kFloatTo;
            g.phase += (float)dt * 6.2831853f / (wolf ? kWolfFloatPeriod : kFloatPeriod);
            const float u = 0.5f - 0.5f * cosf(g.phase);
            const float f = from + (to - from) * u;
            g.frame = (int)f;
            g.t = f - (int)f;
        } else {
            g.t += (float)dt * s * 2 * kAnimSpeed;
            while (g.t >= 1) {
                g.t -= 1;
                g.frame = (g.frame + 1) % nf;
            }
        }
        if (const char* fb = getenv("LUGARUEX_PREVIEW_FRAME")) { // debug: frozen pose
            const float f = (float)atof(fb);
            g.frame = (int)f;
            g.t = f - (int)f;
        }
    }
    static const char* fixedView = getenv("LUGARUEX_PREVIEW_YAW"); // debug: fixed angle
    if (fixedView) {
        g.yaw = (float)atof(fixedView);
        g.pitch = kDefaultPitch;
    } else if (!g.dragging && nowMs - g.lastInput > 3000) {
        // 3 s after the last drag/zoom: glide back to the default front view
        // along the shortest way, with ease-in/ease-out over kReturnTime.
        auto shortest = [](float from, float to) {
            float d = fmodf(to - from, 360.f);
            if (d > 180) {
                d -= 360;
            }
            if (d < -180) {
                d += 360;
            }
            return d;
        };
        if (!g.returning) {
            g.returning = true;
            g.returnT = 0;
            g.fromYaw = g.yaw;
            g.fromPitch = g.pitch;
            g.fromZoom = g.zoom;
            g.dYaw = shortest(g.yaw, kDefaultYaw);
            g.dPitch = shortest(g.pitch, kDefaultPitch);
        }
        if (g.returnT < 1) {
            g.returnT = std::min(1.f, g.returnT + (float)dt / kReturnTime);
            const float u = g.returnT;
            const float e = u * u * u * (u * (u * 6 - 15) + 10); // smootherstep
            g.yaw = g.fromYaw + g.dYaw * e;
            g.pitch = g.fromPitch + g.dPitch * e;
            g.zoom = g.fromZoom + (1.f - g.fromZoom) * e;
        }
    } else {
        g.returning = false; // interrupted by the user: start a fresh glide next time
    }
}

void PreviewCore_Render(int X, int Y, int W, int H)
{
    W = std::max(1, W);
    H = std::max(1, H);
    glViewport(X, Y, W, H);
    glEnable(GL_SCISSOR_TEST);
    glScissor(X, Y, W, H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // background gradient (dark red to black, like the banner)
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBegin(GL_QUADS);
    glColor3f(0.02f, 0.02f, 0.02f);
    glVertex2f(-1, -1);
    glVertex2f(1, -1);
    glColor3f(0.20f, 0.07f, 0.05f);
    glVertex2f(1, 1);
    glVertex2f(-1, 1);
    glEnd();

    const float dim = g.dimmed ? 0.4f : 1.f;
    if (!g.rig.ok) {
        // fallback: flat texture
        if (g.tex) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, g.tex);
            glColor3f(dim, dim, dim);
            const float a = (float)W / H;
            const float sx = a > 1 ? 1 / a : 1, sy = a > 1 ? 1 : a;
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0);
            glVertex2f(-sx * .9f, -sy * .9f);
            glTexCoord2f(1, 0);
            glVertex2f(sx * .9f, -sy * .9f);
            glTexCoord2f(1, 1);
            glVertex2f(sx * .9f, sy * .9f);
            glTexCoord2f(0, 1);
            glVertex2f(-sx * .9f, sy * .9f);
            glEnd();
        }
        glDisable(GL_SCISSOR_TEST);
        return;
    }

    std::vector<V3> J;
    currentJoints(J);
    pose(g.rig, J);

    const float cx = (g.bbMin[0] + g.bbMax[0]) / 2, cy = (g.bbMin[1] + g.bbMax[1]) / 2, cz = (g.bbMin[2] + g.bbMax[2]) / 2;
    const float height = std::max(g.bbMax[1] - g.bbMin[1], std::max(g.bbMax[0] - g.bbMin[0], g.bbMax[2] - g.bbMin[2]));
    const float fovy = 30.f, nearp = 0.05f * height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float fh = tanf(fovy * 3.14159265f / 360.f) * nearp, fw = fh * W / H;
    glFrustum(-fw, fw, -fh, fh, nearp, height * 50);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    const float dist = height * 0.62f / tanf(fovy * 3.14159265f / 360.f) / g.zoom;
    glTranslatef(0, 0, -dist);
    glRotatef(g.pitch, 1, 0, 0);
    glRotatef(g.yaw, 0, 1, 0);
    glTranslatef(-cx, -cy, -cz);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    // light fixed to the camera (front-left-top)
    glPushMatrix();
    glLoadIdentity();
    const GLfloat lpos[4] = { -0.4f, 0.6f, 1.0f, 0 };
    const GLfloat amb[4] = { 0.42f * dim, 0.42f * dim, 0.42f * dim, 1 }, dif[4] = { 0.85f * dim, 0.82f * dim, 0.78f * dim, 1 };
    glLightfv(GL_LIGHT0, GL_POSITION, lpos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glPopMatrix();
    glColor3f(1, 1, 1);
    if (g.tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g.tex);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    } else {
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.6f, 0.5f, 0.4f);
    }
    glBegin(GL_TRIANGLES);
    for (const Tri& t : g.rig.tris) {
        if (!g.rig.owned[t.v[0]] || !g.rig.owned[t.v[1]] || !g.rig.owned[t.v[2]]) {
            continue;
        }
        for (int k = 0; k < 3; k++) {
            const V3& n = g.rig.normals[t.v[k]];
            const V3& p = g.rig.posed[t.v[k]];
            glNormal3f(n.x, n.y, n.z);
            glTexCoord2f(t.u[k], t.t[k]);
            glVertex3f(p.x, p.y, p.z);
        }
    }
    glEnd();

    // ground shadow (soft disc)
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(0, 0, 0, 0.45f);
    glVertex3f(cx, g.bbMin[1], cz);
    glColor4f(0, 0, 0, 0);
    const float rad = height * 0.35f;
    for (int i = 0; i <= 32; i++) {
        float a = i * 6.2831853f / 32;
        glVertex3f(cx + cosf(a) * rad, g.bbMin[1], cz + sinf(a) * rad);
    }
    glEnd();
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
}
