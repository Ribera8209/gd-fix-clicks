// Click Fix — corrige el timing de tus clics en Geometry Dash (todos los vehículos).
// Objetivo: Geometry Dash 2.209x (2.2091) + Geode v6.
//
// Cómo funciona:
//  1. Cada frame guarda una "foto" del estado del nivel (CheckpointObject) en un buffer corto.
//  2. Registra en qué frame diste cada clic (presionar / soltar).
//  3. Si vas a morir y diste un clic hace poco (dentro de la "ventana"), NO te mata todavía:
//     rebobina, prueba mover ese clic unos frames antes o después, y simula hacia adelante.
//  4. Si encuentra un momento en que sobrevives (y sigues vivo "lookahead" frames después),
//     aplica ese clic corregido. Si no, mueres normal.
//
// Como usa la simulación real del juego, funciona igual para cubo, nave, bola, UFO,
// wave, robot, araña y swing.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include <deque>
#include <vector>
#include <climits>
#include <algorithm>

using namespace geode::prelude;

namespace {

constexpr int JUMP_BUTTON = 1;

struct InputEvent {
    int step;
    bool down;
    bool p1;
};

struct Snapshot {
    int step;     // estado ANTES del update número `step`
    float dt;     // dt que se usó en ese update
    Ref<CheckpointObject> cp;
    bool heldP1;
    bool heldP2;
};

struct State {
    int step = 0;
    bool simulating = false;  // corriendo simulación interna
    bool simDied = false;     // murió durante la simulación
    bool pendingFix = false;  // hay una muerte pendiente de corregir
    bool bypass = false;      // dejar pasar la muerte real
    bool heldP1 = false;
    bool heldP2 = false;
    std::deque<Snapshot> snaps;
    std::vector<InputEvent> inputs;

    void reset() {
        step = 0;
        simulating = simDied = pendingFix = bypass = false;
        snaps.clear();
        inputs.clear();
        // heldP1/heldP2 reflejan el botón físico; no se borran.
    }
} g;

bool modEnabled() { return Mod::get()->getSettingValue<bool>("enabled"); }
int windowFrames() { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("window")); }
int lookaheadFrames() { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("lookahead")); }

Snapshot* findSnap(int step) {
    for (auto& s : g.snaps) if (s.step == step) return &s;
    return nullptr;
}

float dtForStep(int step, float fallback) {
    if (auto s = findSnap(step)) return s->dt;
    return fallback;
}

// Carga una foto y deja el botón presionado/suelto como estaba en ese momento.
void restoreSnap(PlayLayer* pl, Snapshot const& s) {
    bool prev = g.simulating;
    g.simulating = true;
    pl->loadFromCheckpoint(s.cp);
    pl->handleButton(false, JUMP_BUTTON, true);
    if (s.heldP1) pl->handleButton(true, JUMP_BUTTON, true);
    if (pl->m_gameState.m_isDualMode) {
        pl->handleButton(false, JUMP_BUTTON, false);
        if (s.heldP2) pl->handleButton(true, JUMP_BUTTON, false);
    }
    g.simulating = prev;
}

// Simula desde `from` hasta `to` (sin incluir) con la lista de inputs dada.
// Devuelve true si el jugador NO murió.
bool simulate(PlayLayer* pl, int from, int to, std::vector<InputEvent> const& evs, float dtNow) {
    auto snap = findSnap(from);
    if (!snap) return false;
    restoreSnap(pl, *snap);

    g.simulating = true;
    g.simDied = false;
    for (int s = from; s < to && !g.simDied; ++s) {
        for (auto const& e : evs) {
            if (e.step == s) pl->handleButton(e.down, JUMP_BUTTON, e.p1);
        }
        pl->update(dtForStep(s, dtNow));
    }
    g.simulating = false;
    return !g.simDied;
}

// Intenta mover el último clic para evitar la muerte.
bool tryFix(PlayLayer* pl, float dtNow) {
    int now = g.step;            // el update que está por correr
    int deathStep = now - 1;     // el update en el que se murió
    int W = windowFrames();
    int L = lookaheadFrames();

    if (g.inputs.empty()) return false;
    auto& ev = g.inputs.back();
    if (deathStep - ev.step > W) return false;

    int prevLimit = g.inputs.size() > 1 ? g.inputs[g.inputs.size() - 2].step : INT_MIN;

    for (int d = 1; d <= W; ++d) {
        for (int sign : {-1, +1}) {          // primero un poco antes, luego un poco después
            int ns = ev.step + sign * d;
            if (ns <= prevLimit || ns > deathStep) continue;
            int from = std::min(ev.step, ns);
            if (!findSnap(from)) continue;

            std::vector<InputEvent> evs;
            for (auto const& e : g.inputs) if (e.step >= from) evs.push_back(e);
            evs.back().step = ns;

            if (simulate(pl, from, now + L, evs, dtNow)) {
                // Sobrevive: volver a simular sólo hasta "ahora" para no adelantar el juego.
                simulate(pl, from, now, evs, dtNow);
                ev.step = ns;
                g.snaps.erase(
                    std::remove_if(g.snaps.begin(), g.snaps.end(),
                        [from](Snapshot const& s) { return s.step > from; }),
                    g.snaps.end());
                log::debug("Click corregido: {} frames", sign * d);
                return true;
            }
        }
    }
    return false;
}

} // namespace

class $modify(CFBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto pl = PlayLayer::get();
        bool ours = pl && static_cast<GJBaseGameLayer*>(pl) == this;

        if (ours && !g.simulating && button == JUMP_BUTTON) {
            if (isPlayer1) g.heldP1 = down; else g.heldP2 = down;
            if (modEnabled()) {
                g.inputs.push_back({ g.step, down, isPlayer1 });
                if (g.inputs.size() > 64) g.inputs.erase(g.inputs.begin());
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void update(float dt) {
        auto pl = PlayLayer::get();
        if (g.simulating || !pl || static_cast<GJBaseGameLayer*>(pl) != this || !modEnabled()) {
            GJBaseGameLayer::update(dt);
            return;
        }

        if (g.pendingFix) {
            g.pendingFix = false;
            if (!tryFix(pl, dt)) {
                // No hubo forma: repetir el frame de la muerte y dejar que pase de verdad.
                int deathStep = g.step - 1;
                if (auto s = findSnap(deathStep)) {
                    restoreSnap(pl, *s);
                    for (auto const& e : g.inputs) {
                        if (e.step == deathStep) {
                            g.simulating = true;
                            pl->handleButton(e.down, JUMP_BUTTON, e.p1);
                            g.simulating = false;
                        }
                    }
                    g.bypass = true;
                    GJBaseGameLayer::update(s->dt);
                    g.bypass = false;
                }
                g.snaps.clear();
                g.inputs.clear();
                return;
            }
        }

        // Guardar foto del estado actual (antes de este update).
        if (pl->m_player1 && !pl->m_player1->m_isDead) {
            if (auto cp = pl->createCheckpoint()) {
                g.snaps.push_back({ g.step, dt, cp, g.heldP1, g.heldP2 });
                size_t maxSnaps = static_cast<size_t>(windowFrames() * 2 + 3);
                while (g.snaps.size() > maxSnaps) g.snaps.pop_front();
            }
        }

        GJBaseGameLayer::update(dt);
        g.step++;
    }
};

class $modify(CFPlayLayer, PlayLayer) {
    // En GD 2.209x la firma es init(level, dontCreateObjects)
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        g.reset();
        return PlayLayer::init(level, dontCreateObjects);
    }

    void resetLevel() {
        if (!g.simulating) g.reset();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        g.reset();
        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (g.simulating) {          // muerte dentro de la simulación: sólo anotarla
            g.simDied = true;
            return;
        }
        if (g.bypass || !modEnabled() || g.step < 2) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }
        if (g.pendingFix) return;    // ya se va a revisar en el siguiente frame

        bool recentClick = !g.inputs.empty() &&
                           (g.step - g.inputs.back().step) <= windowFrames();
        if (!recentClick) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }
        g.pendingFix = true;         // posponer la muerte y tratar de corregir el clic
    }
};
