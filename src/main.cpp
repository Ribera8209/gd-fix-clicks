// Click Fix v0.2 — Geometry Dash 2.209x (2.2091) + Geode v6
//
// Cómo funciona (modo predictivo):
//  1. Cuando das clic (o sueltas), el mod NO se lo pasa al juego de inmediato.
//  2. Simula en secreto: "si salto AHORA, ¿sobrevivo los próximos X ms?"
//  3. Si sí  -> salta ahora (como siempre).
//     Si no  -> prueba retrasar el salto 1, 2, 3... frames hasta encontrar
//               el momento en que sí pasas el pincho, y salta justo ahí.
//  4. Después de cada simulación restaura el estado exacto, así que en
//     pantalla nunca ves el juego adelantarse ni regresar.
//
// Funciona con todos los vehículos porque usa la física real del juego.
// Limitación: sólo puede RETRASAR un clic temprano. Un clic tardío ya pasó
// y no se puede corregir sin regresar el tiempo.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include <deque>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>

using namespace geode::prelude;

namespace {

constexpr int JUMP_BUTTON = 1;

struct NewInput { bool down; bool p1; };
struct PendingInput { double fireTime; bool down; bool p1; };

struct State {
    bool internal = false;    // input generado por el mod (no interceptar)
    bool simulating = false;  // dentro de una simulación
    bool simDied = false;
    double time = 0.0;        // tiempo real acumulado del intento
    double lastShift = 0.0;   // retraso aplicado al último input
    std::vector<NewInput> incoming;
    std::deque<PendingInput> pending;

    void reset() {
        internal = simulating = simDied = false;
        time = 0.0;
        lastShift = 0.0;
        incoming.clear();
        pending.clear();
    }
} g;

bool modEnabled() { return Mod::get()->getSettingValue<bool>("enabled"); }
double maxDelay() { return Mod::get()->getSettingValue<int64_t>("max-delay") / 1000.0; }
double lookahead() { return Mod::get()->getSettingValue<int64_t>("lookahead") / 1000.0; }

// ---------- Guardar / restaurar estado completo ----------

struct TrailSave {
    Ref<CCArray> points;
    CCPoint current;
};

struct SavedState {
    Ref<CheckpointObject> cp;
    double extraDelta = 0;
    int currentStep = 0;
    bool started = false;
    std::vector<PlayerButtonCommand> queued;
    double currentTime = 0;
    double attemptTime = 0;
    std::map<int, bool> hold1, hold2;
    TrailSave wave1, wave2;
};

TrailSave saveTrail(PlayerObject* p) {
    TrailSave t;
    if (p && p->m_waveTrail && p->m_waveTrail->m_pointArray) {
        t.points = CCArray::create();
        t.points->addObjectsFromArray(p->m_waveTrail->m_pointArray);
        t.current = p->m_waveTrail->m_currentPoint;
    }
    return t;
}

void restoreTrail(PlayerObject* p, TrailSave const& t) {
    if (p && p->m_waveTrail && p->m_waveTrail->m_pointArray && t.points) {
        p->m_waveTrail->m_pointArray->removeAllObjects();
        p->m_waveTrail->m_pointArray->addObjectsFromArray(t.points);
        p->m_waveTrail->m_currentPoint = t.current;
    }
}

SavedState saveState(PlayLayer* pl) {
    SavedState s;
    s.cp = pl->createCheckpoint();
    s.extraDelta = pl->m_extraDelta;
    s.currentStep = pl->m_currentStep;
    s.started = pl->m_started;
    s.queued = pl->m_queuedButtons;
    s.currentTime = pl->m_currentTime;
    s.attemptTime = pl->m_attemptTime;
    if (pl->m_player1) { s.hold1 = pl->m_player1->m_holdingButtons; s.wave1 = saveTrail(pl->m_player1); }
    if (pl->m_player2) { s.hold2 = pl->m_player2->m_holdingButtons; s.wave2 = saveTrail(pl->m_player2); }
    return s;
}

void restoreState(PlayLayer* pl, SavedState const& s) {
    if (s.cp) pl->loadFromCheckpoint(s.cp);
    pl->m_extraDelta = s.extraDelta;
    pl->m_currentStep = s.currentStep;
    pl->m_started = s.started;
    pl->m_queuedButtons = s.queued;
    pl->m_currentTime = s.currentTime;
    pl->m_attemptTime = s.attemptTime;
    if (pl->m_player1) { pl->m_player1->m_holdingButtons = s.hold1; restoreTrail(pl->m_player1, s.wave1); }
    if (pl->m_player2) { pl->m_player2->m_holdingButtons = s.hold2; restoreTrail(pl->m_player2, s.wave2); }
}

void sendToGame(PlayLayer* pl, bool down, bool p1) {
    g.internal = true;
    pl->handleButton(down, JUMP_BUTTON, p1);
    g.internal = false;
}

// ---------- Predicción ----------

// Simula: espera `delaySteps` frames, aplica el input, y sigue `lookSteps` frames.
// Devuelve true si el jugador sobrevive.
bool simulateOnce(PlayLayer* pl, NewInput const& in, int delaySteps, int lookSteps, float simDt) {
    g.simulating = true;
    g.simDied = false;
    int total = delaySteps + lookSteps;
    for (int s = 0; s < total && !g.simDied; ++s) {
        if (s == delaySteps) sendToGame(pl, in.down, in.p1);
        pl->update(simDt);
    }
    g.simulating = false;
    return !g.simDied;
}

// Devuelve cuántos segundos hay que retrasar el input (0 = ahora).
double findBestDelay(PlayLayer* pl, NewInput const& in, float dt) {
    if (!pl->m_player1 || pl->m_player1->m_isDead) return 0.0;

    // Paso de simulación: el dt real, pero no menor a 1/120 s para no trabar el juego.
    float simDt = std::max(dt, 1.0f / 120.0f);
    int maxSteps = static_cast<int>(std::ceil(maxDelay() / simDt));
    int lookSteps = std::max(1, static_cast<int>(std::ceil(lookahead() / simDt)));

    auto& audio = *FMODAudioEngine::sharedEngine();
    float sfxVol = audio.m_sfxVolume;
    audio.m_sfxVolume = 0.f;   // sin sonidos durante la simulación

    SavedState saved = saveState(pl);
    double result = 0.0;
    for (int k = 0; k <= maxSteps; ++k) {
        bool ok = simulateOnce(pl, in, k, lookSteps, simDt);
        restoreState(pl, saved);
        if (ok) { result = k * simDt; break; }
    }

    audio.m_sfxVolume = sfxVol;
    if (result > 0) log::debug("Click Fix: input retrasado {:.0f} ms", result * 1000.0);
    return result;
}

} // namespace

class $modify(CFBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto pl = PlayLayer::get();
        bool ours = pl && static_cast<GJBaseGameLayer*>(pl) == this;

        // Interceptar sólo clics reales del jugador en un nivel.
        if (ours && !g.internal && !g.simulating && button == JUMP_BUTTON && modEnabled()) {
            g.incoming.push_back({ down, isPlayer1 });
            return;
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void update(float dt) {
        auto pl = PlayLayer::get();
        if (g.simulating || !pl || static_cast<GJBaseGameLayer*>(pl) != this) {
            GJBaseGameLayer::update(dt);
            return;
        }

        // Si se apaga el mod con inputs en espera, soltarlos de inmediato.
        if (!modEnabled()) {
            for (auto& p : g.pending) sendToGame(pl, p.down, p.p1);
            for (auto& n : g.incoming) sendToGame(pl, n.down, n.p1);
            g.pending.clear();
            g.incoming.clear();
            GJBaseGameLayer::update(dt);
            return;
        }

        // 1. Decidir cuándo aplicar cada input nuevo.
        for (auto const& in : g.incoming) {
            double delay;
            if (!g.pending.empty()) {
                // Ya hay un input esperando: mantener el mismo retraso para respetar el orden.
                delay = g.lastShift;
            } else {
                delay = findBestDelay(pl, in, dt);
                g.lastShift = delay;
            }
            double fire = g.time + delay;
            if (!g.pending.empty()) fire = std::max(fire, g.pending.back().fireTime);
            g.pending.push_back({ fire, in.down, in.p1 });
        }
        g.incoming.clear();

        // 2. Aplicar los inputs cuyo momento ya llegó.
        while (!g.pending.empty() && g.pending.front().fireTime <= g.time + 1e-6) {
            auto p = g.pending.front();
            g.pending.pop_front();
            sendToGame(pl, p.down, p.p1);
        }

        // 3. Avanzar el juego normal.
        GJBaseGameLayer::update(dt);
        g.time += dt;
    }
};

class $modify(CFPlayLayer, PlayLayer) {
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
        if (g.simulating) {   // muerte dentro de la simulación: sólo anotarla
            g.simDied = true;
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }
};
