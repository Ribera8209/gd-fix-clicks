// Click Fix v0.3 — Geometry Dash 2.209x (2.2091) + Geode v6
//
// Cómo funciona (jugador fantasma):
//  - Al dar clic, el mod NO toca el nivel. Usa una copia invisible del
//    jugador ("fantasma") y la mueve con la física real del juego, chocando
//    sólo contra bloques, pinchos y rampas.
//  - Prueba: "si salto ahora, ¿el fantasma sobrevive?" Si no, prueba
//    saltar 1, 2, 3... frames después hasta encontrar el momento que sí pasa.
//  - El salto real se da en ese frame, de forma normal.
//  - El jugador real y el nivel nunca se cargan ni se regresan:
//    sin teletransporte y sin trabones.
//
// Técnica basada en "Show Trajectory" de Eclipse Menu (código abierto).
// Limitación: sólo puede RETRASAR un clic temprano; no corrige clics tardíos.
// El fantasma ignora orbes, pads y portales, así que cerca de ellos la
// predicción puede fallar.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GameObject.hpp>

#include <deque>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace geode::prelude;

namespace {

constexpr int JUMP_BUTTON = 1;

struct NewInput { bool down; bool p1; };
struct PendingInput { int fireFrame; bool down; bool p1; };

struct State {
    // fantasmas
    PlayerObject* ghost = nullptr;
    bool simulating = false;
    bool simDied = false;

    // inputs
    bool internal = false;
    int frame = 0;               // frames reales del intento
    int lastShift = 0;           // retraso (en frames) del último input
    bool heldP1 = false, heldP2 = false;  // lo que ya se le mandó al juego
    std::vector<NewInput> incoming;
    std::deque<PendingInput> pending;

    // medición de física
    float tickDt = 0.25f;        // dt de cada tick de física (unidades de GD)
    int ticksThisFrame = 0;
    int ticksPerFrame = 4;

    void resetInputs() {
        internal = false;
        frame = 0;
        lastShift = 0;
        heldP1 = heldP2 = false;
        incoming.clear();
        pending.clear();
        ticksThisFrame = 0;
    }
} g;

bool modEnabled() { return Mod::get()->getSettingValue<bool>("enabled"); }
double maxDelayMs() { return static_cast<double>(Mod::get()->getSettingValue<int64_t>("max-delay")); }
double lookaheadMs() { return static_cast<double>(Mod::get()->getSettingValue<int64_t>("lookahead")); }

void sendToGame(PlayLayer* pl, bool down, bool p1) {
    if (p1) g.heldP1 = down; else g.heldP2 = down;
    g.internal = true;
    pl->handleButton(down, JUMP_BUTTON, p1);
    g.internal = false;
}

void resetCollisionLog(PlayerObject* p) {
    if (p->m_collisionLogTop) p->m_collisionLogTop->removeAllObjects();
    if (p->m_collisionLogBottom) p->m_collisionLogBottom->removeAllObjects();
    if (p->m_collisionLogLeft) p->m_collisionLogLeft->removeAllObjects();
    if (p->m_collisionLogRight) p->m_collisionLogRight->removeAllObjects();
}

// Prepara el fantasma como copia del jugador real.
void prepareGhost(PlayerObject* ghost, PlayerObject* base, bool held) {
    ghost->copyAttributes(base);
    ghost->m_gravityMod = base->m_gravityMod;
    if (held) ghost->pushButton(PlayerButton::Jump);
    else ghost->releaseButton(PlayerButton::Jump);
    ghost->m_isOnGround = base->m_isOnGround;
    ghost->setVisible(false);
}

// Simula: espera `delayTicks`, aplica el input, sigue `lookTicks`.
// true = el fantasma sobrevive.
bool simulateGhost(PlayLayer* pl, PlayerObject* base, bool held,
                   NewInput const& in, int delayTicks, int lookTicks) {
    auto ghost = g.ghost;
    prepareGhost(ghost, base, held);

    g.simulating = true;
    g.simDied = false;
    int total = delayTicks + lookTicks;
    for (int t = 0; t < total; ++t) {
        resetCollisionLog(ghost);
        pl->checkCollisions(ghost, g.tickDt, false);
        if (g.simDied) break;
        if (t == delayTicks) {
            if (in.down) ghost->pushButton(PlayerButton::Jump);
            else ghost->releaseButton(PlayerButton::Jump);
        }
        ghost->update(g.tickDt);
    }
    g.simulating = false;
    return !g.simDied;
}

// Devuelve cuántos frames reales hay que retrasar el input (0 = ahora).
int findBestDelay(PlayLayer* pl, NewInput const& in, float frameDt) {
    PlayerObject* base = in.p1 ? pl->m_player1 : pl->m_player2;
    if (!g.ghost || !base || base->m_isDead) return 0;

    bool held = in.p1 ? g.heldP1 : g.heldP2;
    int tpf = std::max(1, g.ticksPerFrame);
    double frameMs = std::max(1.0, frameDt * 1000.0);
    int maxFrames = static_cast<int>(maxDelayMs() / frameMs);
    double tickMs = frameMs / tpf;
    int lookTicks = std::max(1, static_cast<int>(lookaheadMs() / tickMs));

    for (int k = 0; k <= maxFrames; ++k) {
        if (simulateGhost(pl, base, held, in, k * tpf, lookTicks)) {
            if (k > 0) log::debug("Click Fix: salto retrasado {} frames", k);
            return k;
        }
    }
    return 0; // nada funciona: comportamiento normal
}

} // namespace

// ---------------- Hooks principales ----------------

class $modify(CFBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto pl = PlayLayer::get();
        bool ours = pl && static_cast<GJBaseGameLayer*>(pl) == this;
        if (ours && !g.internal && button == JUMP_BUTTON && modEnabled()) {
            g.incoming.push_back({ down, isPlayer1 });
            return;
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void update(float dt) {
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this) {
            GJBaseGameLayer::update(dt);
            return;
        }

        if (!modEnabled()) {
            for (auto& p : g.pending) sendToGame(pl, p.down, p.p1);
            for (auto& n : g.incoming) sendToGame(pl, n.down, n.p1);
            g.pending.clear();
            g.incoming.clear();
        } else {
            // 1. Decidir el frame de cada input nuevo.
            for (auto const& in : g.incoming) {
                int shift = g.pending.empty() ? findBestDelay(pl, in, dt) : g.lastShift;
                g.lastShift = shift;
                int fire = g.frame + shift;
                if (!g.pending.empty()) fire = std::max(fire, g.pending.back().fireFrame);
                g.pending.push_back({ fire, in.down, in.p1 });
            }
            g.incoming.clear();

            // 2. Mandar al juego los que ya toca.
            while (!g.pending.empty() && g.pending.front().fireFrame <= g.frame) {
                auto p = g.pending.front();
                g.pending.pop_front();
                sendToGame(pl, p.down, p.p1);
            }
        }

        // 3. Juego normal.
        g.ticksThisFrame = 0;
        GJBaseGameLayer::update(dt);
        if (g.ticksThisFrame > 0) g.ticksPerFrame = g.ticksThisFrame;
        g.frame++;
    }

    // --- bloquear efectos del fantasma ---
    void collisionCheckObjects(PlayerObject* player, std::vector<GameObject*>* objects, int count, float dt) {
        if (g.simulating) {
            std::vector<GameObject*> filtered;
            for (int i = 0; i < count; i++) {
                auto obj = objects->at(i);
                auto t = obj->m_objectType;
                if (t == GameObjectType::Solid || t == GameObjectType::Hazard ||
                    t == GameObjectType::AnimatedHazard || t == GameObjectType::Slope) {
                    filtered.push_back(obj);
                }
            }
            GJBaseGameLayer::collisionCheckObjects(player, &filtered, static_cast<int>(filtered.size()), dt);
            return;
        }
        GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
    }

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* obj) {
        if (g.simulating) return false;
        return GJBaseGameLayer::canBeActivatedByPlayer(player, obj);
    }

    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        if (g.simulating) return;
        GJBaseGameLayer::flipGravity(player, flip, noEffects);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (g.simulating) return;
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }
};

class $modify(CFPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        g.ghost = nullptr;
        g.resetInputs();
        if (!PlayLayer::init(level, dontCreateObjects)) return false;

        auto ghost = PlayerObject::create(1, 1, this, this, true);
        ghost->setPosition({ 0, 105 });
        ghost->setVisible(false);
        ghost->setID("click-fix-ghost"_spr);
        m_objectLayer->addChild(ghost);
        g.ghost = ghost;
        return true;
    }

    void resetLevel() {
        g.resetInputs();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        g.ghost = nullptr;
        g.resetInputs();
        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (g.simulating && player == g.ghost) {
            g.simDied = true;
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void playEndAnimationToPos(CCPoint pos) {
        if (g.simulating) return;
        PlayLayer::playEndAnimationToPos(pos);
    }
};

class $modify(CFPlayerObject, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        if (g.simulating) return;
        auto pl = PlayLayer::get();
        if (pl && this == pl->m_player1) {
            float warp = pl->m_gameState.m_timeWarp;
            g.tickDt = warp > 0.f ? dt / warp : dt;
            g.ticksThisFrame++;
        }
    }

    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (g.simulating) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }

    void incrementJumps() {
        if (g.simulating) return;
        PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* ring, bool skipCheck) {
        if (g.simulating) return;
        PlayerObject::ringJump(ring, skipCheck);
    }
};

class $modify(CFHardStreak, HardStreak) {
    void addPoint(CCPoint p) {
        if (g.simulating) return;
        HardStreak::addPoint(p);
    }
};

class $modify(CFEffectGameObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, std::vector<int> const* remapKeys) {
        if (g.simulating) return;
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};

class $modify(CFGameObject, GameObject) {
    void playShineEffect() {
        if (g.simulating) return;
        GameObject::playShineEffect();
    }
};
