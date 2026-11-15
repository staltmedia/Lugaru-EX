/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Online chat: T opens a line to type (unless T is bound to a game action). Each line
// goes to the console log as "name: text" and floats over the speaker's head for a few
// seconds, a little see-through, fading out; nearer speakers' text is bigger.

#include "EX/ExNet.hpp"

#include "EX/ExLocal.hpp"
#include "Game.hpp"
#include "Graphic/gamegl.hpp"
#include "Objects/Person.hpp"
#include "netinfo.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

extern float screenwidth, screenheight;
extern int mainmenu;

namespace EX
{
namespace
{
bool g_typing = false;
bool g_hold = false; // the keys that closed the line are still down: the game must not see them
std::string g_line;
Uint32 g_openedAt = 0;

struct Bubble
{
    int person;
    std::string text;
    Uint32 at;
    bool afk = false; // an AFK countdown (seconds), not a chat line
    bool typing = false; // "..." while that player writes a line
};
std::vector<Bubble> g_bubbles;
const Uint32 kShow = 6000, kFade = 2000; // ms

double g_mv[16], g_pj[16];
int g_vp[4];
bool g_have3D = false;

bool keyBound(int scancode)
{
    const unsigned short keys[] = { Game::crouchkey, Game::jumpkey, Game::forwardkey, Game::backkey, Game::leftkey,
                                    Game::rightkey,  Game::drawkey, Game::throwkey,   Game::attackkey, Game::consolekey };
    for (unsigned short k : keys) {
        if (k == scancode) {
            return true;
        }
    }
    return false;
}

void closeLine()
{
    g_typing = false;
    g_hold = true;
    g_line.clear();
    SDL_StopTextInput();
}

void logLine(const std::string& l)
{
    for (int k = 14; k >= 2; k--) {
        Game::consoletext[k] = Game::consoletext[k - 1];
    }
    Game::consoletext[1] = l;
    printf("LugaruEX chat: %s\n", l.c_str());
}

// text in lines of at most n characters, broken at spaces where it can
std::vector<std::string> wrap(const std::string& s, size_t n)
{
    std::vector<std::string> out;
    size_t p = 0;
    while (p < s.size()) {
        size_t len = std::min(n, s.size() - p);
        if (p + len < s.size()) {
            const size_t sp = s.rfind(' ', p + len);
            if (sp != std::string::npos && sp > p) {
                len = sp - p;
            }
        }
        out.push_back(s.substr(p, len));
        p += len;
        while (p < s.size() && s[p] == ' ') {
            p++;
        }
    }
    return out;
}

void textState()
{
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
}
} // namespace

bool ChatHandleEvent(const void* ev)
{
    const SDL_Event& e = *(const SDL_Event*)ev;
    if (g_typing) {
        switch (e.type) {
            case SDL_TEXTINPUT:
                // (the T that opened the line can arrive as text right after)
                if (SDL_GetTicks() - g_openedAt < 150 && (e.text.text[0] == 't' || e.text.text[0] == 'T') && e.text.text[1] == 0 && g_line.empty()) {
                    return true;
                }
                g_line += e.text.text;
                if (g_line.size() > (size_t)netinfo::kMaxChat) {
                    g_line.resize(netinfo::kMaxChat);
                }
                return true;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    const std::string l = g_line;
                    closeLine();
                    NetChatSend(l);
                } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                    closeLine();
                } else if (e.key.keysym.sym == SDLK_BACKSPACE && !g_line.empty()) {
                    g_line.pop_back();
                    while (!g_line.empty() && ((unsigned char)g_line.back() & 0xC0) == 0x80) {
                        g_line.pop_back(); // the rest of a multi-byte character
                    }
                    if (!g_line.empty() && ((unsigned char)g_line.back() & 0xC0) == 0xC0) {
                        g_line.pop_back();
                    }
                }
                return true;
            case SDL_KEYUP:
            case SDL_TEXTEDITING:
                return true;
        }
        return false;
    }
    if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_T && !(e.key.keysym.mod & (KMOD_ALT | KMOD_CTRL | KMOD_GUI)) &&
        NetOnline() && Game::gameon && !mainmenu && !Game::console && !Game::loading && !keyBound(SDL_SCANCODE_T)) {
        g_typing = true;
        g_line.clear();
        g_openedAt = SDL_GetTicks();
        SDL_StartTextInput();
        return true;
    }
    return false;
}

bool ChatTyping()
{
    // test hook LUGARUEX_TEST_TYPING=from-to: "writing" from..to seconds of level time
    static const char* tt = getenv("LUGARUEX_TEST_TYPING");
    if (tt && Game::gameon && strchr(tt, '-')) {
        const float t = Game::leveltime;
        if (t >= (float)atof(tt) && t < (float)atof(strchr(tt, '-') + 1)) {
            return true;
        }
    }
    return g_typing;
}

bool ChatBlocksKeys()
{
    if (g_typing) {
        return true;
    }
    if (g_hold) {
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        if (!k[SDL_SCANCODE_RETURN] && !k[SDL_SCANCODE_KP_ENTER] && !k[SDL_SCANCODE_ESCAPE]) {
            g_hold = false;
        }
    }
    return g_hold;
}

void ChatReceived(int person, const std::string& name, const std::string& text)
{
    if (person < 0) {
        logLine(text); // joined / left
        return;
    }
    logLine(name + ": " + text);
    // lines sent one after another stack over the head (newest nearest), each fading on its
    // own clock; at most four per player
    int mine = 0;
    for (const Bubble& b : g_bubbles) {
        mine += b.person == person ? 1 : 0;
    }
    for (auto it = g_bubbles.begin(); mine >= 4 && it != g_bubbles.end();) {
        if (it->person == person) {
            it = g_bubbles.erase(it); // (the oldest go first: the list is in arrival order)
            mine--;
        } else {
            ++it;
        }
    }
    Bubble b;
    b.person = person;
    b.text = text;
    b.at = SDL_GetTicks();
    g_bubbles.push_back(b);
}

// something over someone's head besides chat: an AFK countdown, or "..." while they write
static bool anyOverHead()
{
    for (auto& p : Person::players) {
        if ((p->exAfk > 0 && p->exAfk <= 60 && !p->dead) || p->exTyping) {
            return true;
        }
    }
    return false;
}

void ChatCapture3D()
{
    if (g_bubbles.empty() && !anyOverHead()) {
        g_have3D = false; // (nothing over anyone's head: no need to read the matrices back)
        return;
    }
    glGetDoublev(GL_MODELVIEW_MATRIX, g_mv);
    glGetDoublev(GL_PROJECTION_MATRIX, g_pj);
    glGetIntegerv(GL_VIEWPORT, g_vp);
    g_have3D = true;
}

void ChatDraw()
{
    const Uint32 now = SDL_GetTicks();
    g_bubbles.erase(std::remove_if(g_bubbles.begin(), g_bubbles.end(), [&](const Bubble& b) { return now - b.at > kShow + kFade; }), g_bubbles.end());
    const bool line = g_typing && DrawSharedHud();
    // AFK countdowns over heads (host option AFK Timer): the last minute before the AI takes over
    std::vector<Bubble> shown = g_bubbles;
    for (size_t i = 0; i < Person::players.size(); i++) {
        const Person& p = *Person::players[i];
        if (p.exAfk > 0 && p.exAfk <= 60 && !p.dead) {
            Bubble b;
            b.person = (int)i;
            b.text = std::to_string((int)p.exAfk);
            b.at = now;
            b.afk = true;
            shown.push_back(b);
        }
        if (p.exTyping && !p.dead) {
            Bubble b;
            b.person = (int)i;
            b.text = std::string("...").substr(0, 1 + (now / 400) % 3); // . .. ... while they write
            b.at = now;
            b.typing = true;
            shown.push_back(b);
        }
    }

    if ((shown.empty() || !g_have3D) && !line) {
        g_have3D = false;
        return;
    }
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    textState();
    struct Placed
    {
        float x0, x1, y0, y1; // screen box (y up)
        float size, alpha;
        double dist;
        std::vector<std::string> lines;
        float sx;
        bool afk, typing;
        int person;
        Uint32 at;
    };
    std::vector<Placed> placed;
    for (const Bubble& b : g_have3D ? shown : std::vector<Bubble>()) {
        if (b.person < 0 || b.person >= (int)Person::players.size() || HiddenInScene(b.person)) {
            continue;
        }
        Person& p = *Person::players[b.person];
        XYZ at = DoRotation(p.jointPos(head), 0, p.yaw, 0) * p.scale + p.coords;
        at.y += 0.4f;
        // to the screen with the camera this view was drawn with
        double v[4] = { at.x, at.y, at.z, 1 }, eye[4], clip[4];
        for (int r = 0; r < 4; r++) {
            eye[r] = g_mv[r] * v[0] + g_mv[4 + r] * v[1] + g_mv[8 + r] * v[2] + g_mv[12 + r] * v[3];
        }
        for (int r = 0; r < 4; r++) {
            clip[r] = g_pj[r] * eye[0] + g_pj[4 + r] * eye[1] + g_pj[8 + r] * eye[2] + g_pj[12 + r] * eye[3];
        }
        const double dist = clip[3];
        if (dist < 0.3 || dist > 60) {
            continue; // behind the camera, or too far to read
        }
        const double sx = (clip[0] / dist * 0.5 + 0.5) * g_vp[2], sy = (clip[1] / dist * 0.5 + 0.5) * g_vp[3];
        // nearer = bigger; far text stays small but readable, then fades with distance
        const float scale = screenheight / 768.f;
        const float size = std::max(0.55f, std::min(1.5f, (float)(5.0 / dist))) * scale;
        const Uint32 age = now - b.at;
        float alpha = 0.85f * (age < kShow ? 1.f : 1.f - (float)(age - kShow) / kFade);
        alpha *= dist > 35 ? std::max(0.f, (float)(1 - (dist - 35) / 25)) : 1.f;
        if (alpha <= 0.01f) {
            continue;
        }
        Placed pl;
        pl.lines = wrap(b.text, 36);
        pl.size = size;
        pl.alpha = alpha;
        pl.dist = dist;
        pl.sx = (float)sx;
        pl.afk = b.afk;
        pl.typing = b.typing;
        pl.person = b.person;
        pl.at = b.at;
        if (b.afk || b.typing) {
            pl.size *= 1.6f; // the countdown and the writing dots read from further away
        }
        size_t widest = 0;
        for (auto& l : pl.lines) {
            widest = std::max(widest, l.size());
        }
        const float lh = 22 * pl.size;
        pl.x0 = (float)sx - widest * 10.f * pl.size / 2;
        pl.x1 = (float)sx + widest * 10.f * pl.size / 2;
        pl.y0 = (float)sy;
        pl.y1 = (float)sy + lh * pl.lines.size();
        placed.push_back(pl);
    }
    // nearest speaker keeps their place; the others move up until nothing overlaps
    // (one player's own stack: countdown, then "...", then their lines newest first - the
    // later ones move up, so the newest line sits nearest the head and older ones rise above)
    auto rank = [](const Placed& p) { return p.afk ? 0 : p.typing ? 1 : 2; };
    std::stable_sort(placed.begin(), placed.end(), [&rank](const Placed& a, const Placed& b) {
        if (a.dist != b.dist) {
            return a.dist < b.dist;
        }
        if (rank(a) != rank(b)) {
            return rank(a) < rank(b);
        }
        return a.at > b.at;
    });
    for (size_t i = 0; i < placed.size(); i++) {
        for (int guard = 0; guard < 16; guard++) {
            bool moved = false;
            for (size_t k = 0; k < i; k++) {
                const Placed& o = placed[k];
                Placed& m = placed[i];
                if (m.x0 < o.x1 && o.x0 < m.x1 && m.y0 < o.y1 && o.y0 < m.y1) {
                    const float h = m.y1 - m.y0;
                    m.y0 = o.y1 + 2;
                    m.y1 = m.y0 + h;
                    moved = true;
                }
            }
            if (!moved) {
                break;
            }
        }
    }
    for (const Placed& pl : placed) {
        const float lh = 22 * pl.size;
        float y = pl.y0 + lh * (pl.lines.size() - 1);
        for (const std::string& l : pl.lines) {
            const float x = pl.sx - l.size() * 10.f * pl.size / 2;
            if (pl.afk) { // orange, red for the last ten seconds
                const bool last = atoi(l.c_str()) <= 10;
                Game::text->glPrintOutlined(1, last ? .25f : .6f, last ? .2f : .15f, 1, x, y, l, 1, pl.size, screenwidth, screenheight);
            } else {
                Game::text->glPrintOutlined(1, 1, 1, pl.alpha, x, y, l, 1, pl.size, screenwidth, screenheight);
            }
            y -= lh;
        }
    }
    if (line) {
        const float fullH = SharedHudHeight();
        const float size = 1.2f * fullH / 768.f;
        const bool blink = (now / 400) % 2 == 0;
        const std::string typed = "Say: " + g_line + (blink ? "_" : " ");
        Game::text->glPrintOutlined(1, 1, .85f, 1, 20 * fullH / 768.f, 120 * fullH / 768.f, typed, 1, size, screenwidth, screenheight);
    }
    glPopAttrib();
    g_have3D = false;
}
} // namespace EX

namespace EX
{
// everyone is down (co-op): how to restart, in each view over the death fade
void ChatDrawHint()
{
    const std::string hint = LocalDeadHint();
    if (hint.empty() || !Game::text) {
        return;
    }
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    textState();
    const float size = 1.3f * SharedHudHeight() / 768.f;
    const float x = screenwidth / 2 - hint.size() * 10.f * size / 2;
    Game::text->glPrintOutlined(1, 1, 1, 1, x, screenheight * .12f, hint, 1, size, screenwidth, screenheight);
    glPopAttrib();
}
} // namespace EX
