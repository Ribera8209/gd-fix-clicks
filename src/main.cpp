// Click Fix v0.4 — Predictive Input Buffer para Geometry Dash 2.209x (2.2091) + Geode v6
//
// Cómo funciona:
//  - Al dar clic, una copia invisible del jugador (fantasma) simula el salto
//    con la física real del juego, chocando sólo con bloques, pinchos y rampas.
//  - Si saltar ahora sobrevive -> el clic pasa normal, sin retraso.
//  - Si saltar ahora choca -> busca el PRIMER tick (1/240 s) en que el salto
//    sí sobrevive y lo guarda; el salto se da exactamente en ese tick.
//  - El nivel y el jugador real nunca se cargan ni se regresan.
//
// Ideas basadas en "Show Trajectory" (Eclipse Menu) y en los mods
// "Predictive Input Buffer" de la comunidad. Implementación propia.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>

#include <vector>
#include <algorithm>

using namespace geode::prelude;

namespace {

constexpr int JUMP = 1;
constexpr int SPAM_GAP_TICKS = 8;     // inputs más juntos que esto cuentan como "spam"
constexpr int SPAM_STREAK = 3;        // a partir de 3 seguidos en wave, no se retrasa

bool modEnabled() { return Mod::get()->getSettingValue<bool>("enabled"); }
int maxDelayTicks() { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-delay-ticks")); }
int maxDelayTicksWave() { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-delay-ticks-wave")); }

struct Buffer {
    Ref<PlayerObject> ghost1;
    Ref<PlayerObject> ghost2;

    bool simulating = false;
    bool simDied = false;
    bool firing = false;          // input generado por el mod

    bool physicalHold = false;    // lo que el jugador tiene presionado de verdad
    bool gameHold = false;        // lo que el juego cree

    int waitTicks = -1;           // -1 nada pendiente; >=0 ticks restantes
    bool bufferedPush = true;
    bool queuedOpposite = false;  // soltó/presionó otra vez mientras esperaba
    bool fireOppositeNext = false;

    float tickDt = 0.25f;
    int ticksSinceInput = 999;
    int spamStreak = 0;

    void clearPending() {
        waitTicks = -1;
        queuedOpposite = false;
        fireOppositeNext = false;
    }

    void resetAll() {
        clearPending();
        simulating = simDied = firing = false;
        physicalHold = gameHold = false;
        ticksSinceInput = 999;
        spamStreak = 0;
    }
} b;

bool isDual(PlayLayer* pl) { return pl->m_gameState.m_isDualMode && pl->m_player2; }
bool twoPlayerControls(PlayLayer* pl) { return pl->m_level && pl->m_level->m_twoPlayerMode; }

void fire(PlayLayer* pl, bool push) {
    b.firing = true;
    b.gameHold = push;
    pl->handleButton(push, JUMP, true);
    if (isDual(pl) && !twoPlayerControls(pl)) pl->handleButton(push, JUMP, false);
    b.firing = false;
}

PlayerObject* makeGhost(PlayLayer* pl) {
    auto p = PlayerObject::create(1, 1, pl, pl, true);
    p->setPosition({ 0, 105 });
    p->setVisible(false);
    pl->m_objectLayer->addChild(p);
    return p;
}

void clearCollisionLog(PlayerObject* p) {
    if (p->m_collisionLogTop) p->m_collisionLogTop->removeAllObjects();
    if (p->m_collisionLogBottom) p->m_collisionLogBottom->removeAllObjects();
    if (p->m_collisionLogLeft) p->m_collisionLogLeft->removeAllObjects();
    if (p->m_collisionLogRight) p->m_collisionLogRight->removeAllObjects();
}

void syncGhost(PlayerObject* g, PlayerObject* real) {
    g->setVisible(false);
    g->copyAttributes(real);
    g->setScale(real->getScale());
    g->setContentSize(real->getContentSize());
    g->m_width = real->m_width;
    g->m_height = real->m_height;
    g->m_gravityMod = real->m_gravityMod;
    g->m_isOnGround = real->m_isOnGround;
    g->m_isUpsideDown = real->m_isUpsideDown;
    g->m_vehicleSize = real->m_vehicleSize;
    g->m_playerSpeed = real->m_playerSpeed;
}

void ghostButton(PlayerObject* g, bool push) {
    if (push) g->pushButton(PlayerButton::Jump);
    else g->releaseButton(PlayerButton::Jump);
}

// Cuántos ticks sobrevive el fantasma si el input se aplica tras `delay` ticks.
int survivalTicks(PlayLayer* pl, int delay, bool push, int horizon) {
    auto real1 = pl->m_player1;
    auto real2 = isDual(pl) ? pl->m_player2 : nullptr;
    PlayerObject* g1 = b.ghost1;
    PlayerObject* g2 = real2 ? static_cast<PlayerObject*>(b.ghost2) : nullptr;
    if (!g1 || !real1) return horizon;

    b.simulating = true;
    b.simDied = false;

    syncGhost(g1, real1);
    if (g2) syncGhost(g2, real2);

    // Antes del input, el botón sigue como estaba.
    if (delay > 0) {
        ghostButton(g1, !push);
        if (g2) ghostButton(g2, !push);
    }

    bool continuous = real1->m_isDart || real1->m_isBird || real1->m_isSwing ||
                      (real2 && (real2->m_isDart || real2->m_isBird || real2->m_isSwing));
    int survived = 0, airborne = 0, landed = 0;
    float dt = (b.tickDt > 0.001f && b.tickDt < 2.f) ? b.tickDt : 0.25f;

    for (int i = 0; i < delay + horizon; ++i) {
        if (i == delay) {
            ghostButton(g1, push);
            if (g2) ghostButton(g2, push);
        }

        g1->update(dt);
        g1->updateRotation(dt);
        if (g2) { g2->update(dt); g2->updateRotation(dt); }
        if (b.simDied) break;

        clearCollisionLog(g1);
        pl->checkCollisions(g1, dt, false);
        if (b.simDied) break;
        if (g2) {
            clearCollisionLog(g2);
            pl->checkCollisions(g2, dt, false);
            if (b.simDied) break;
        }

        if (i >= delay) {
            survived++;
            // Cubo/bola/araña/robot: si ya saltó y aterrizó seguro, es suficiente.
            if (!continuous) {
                bool grounded = g1->m_isOnGround && (!g2 || g2->m_isOnGround);
                if (!grounded) { airborne++; landed = 0; }
                else if (airborne >= 15 && ++landed >= 10) { survived = horizon; break; }
            }
        }
    }

    b.simulating = false;
    return survived;
}

// true = mandar el input al juego ahora; false = el mod lo retiene.
bool intercept(PlayLayer* pl, bool push, bool isP1) {
    if (!isP1) {
        // En dual de un solo jugador, P2 sigue a P1.
        if (isDual(pl) && !twoPlayerControls(pl) && (b.waitTicks >= 0 || b.fireOppositeNext)) return false;
        return true;
    }

    b.physicalHold = push;
    if (!modEnabled()) { b.gameHold = push; return true; }

    auto p1 = pl->m_player1;
    auto p2 = isDual(pl) ? pl->m_player2 : nullptr;
    if (!p1 || p1->m_isDead) { b.clearPending(); b.gameHold = push; return true; }

    // La nave no se toca: su control es continuo.
    if (p1->m_isShip || (p2 && p2->m_isShip)) { b.clearPending(); b.gameHold = push; return true; }

    // Ya hay un input esperando: guardar el contrario para después.
    if (b.waitTicks >= 0) {
        b.queuedOpposite = (push != b.bufferedPush);
        b.ticksSinceInput = 0;
        return false;
    }
    if (b.fireOppositeNext) {
        b.fireOppositeNext = false;
        if (b.gameHold != !b.bufferedPush) fire(pl, !b.bufferedPush);
    }

    bool wave = p1->m_isDart || (p2 && p2->m_isDart);
    bool miniWave = (p1->m_isDart && p1->m_vehicleSize < 1.f) || (p2 && p2->m_isDart && p2->m_vehicleSize < 1.f);

    // Spam en wave: no retrasar.
    b.spamStreak = (b.ticksSinceInput < SPAM_GAP_TICKS) ? b.spamStreak + 1 : 0;
    b.ticksSinceInput = 0;
    if (wave && b.spamStreak >= SPAM_STREAK) { b.gameHold = push; return true; }

    // Soltar sólo cambia la física en wave y robot.
    bool releaseMatters = p1->m_isDart || p1->m_isRobot || (p2 && (p2->m_isDart || p2->m_isRobot));
    if (!push && !releaseMatters) { b.gameHold = false; return true; }

    b.queuedOpposite = false;
    int horizon = miniWave ? 50 : (wave ? 65 : 160);
    int maxDelay = wave ? maxDelayTicksWave() : maxDelayTicks();

    int now = survivalTicks(pl, 0, push, horizon);
    if (now >= horizon) { b.gameHold = push; return true; }

    int best = 0, bestTicks = now;
    for (int d = 1; d <= maxDelay; ++d) {
        int t = survivalTicks(pl, d, push, horizon);
        if (wave) {
            // En wave, retrasar un giro siempre "gana" unos ticks; exigir ganancia real.
            if (t >= horizon && t - now >= d * 2 + 6) { best = d; bestTicks = t; break; }
            if (t >= now + d * 2 + 10 && t > bestTicks + 2) { best = d; bestTicks = t; }
        } else {
            if (t >= horizon) { best = d; bestTicks = t; break; }
            if (t > bestTicks + 12) { best = d; bestTicks = t; }
        }
    }

    if (best > 0) {
        log::debug("Click Fix: {} retrasado {} ticks ({} -> {})", push ? "clic" : "soltar", best, now, bestTicks);
        b.bufferedPush = push;
        b.waitTicks = best;
        return false;
    }
    b.gameHold = push;
    return true;
}

// Se llama una vez por tick de física, antes de mover al jugador 1.
void onTick(PlayLayer* pl) {
    if (!modEnabled()) return;
    if (b.ticksSinceInput < 999) b.ticksSinceInput++;

    auto p1 = pl->m_player1;
    auto p2 = isDual(pl) ? pl->m_player2 : nullptr;
    if (p1 && (p1->m_isShip || (p2 && p2->m_isShip))) {
        b.clearPending();
        if (b.gameHold != b.physicalHold) fire(pl, b.physicalHold);
        return;
    }

    if (b.waitTicks == 0) {
        fire(pl, b.bufferedPush);
        b.fireOppositeNext = b.queuedOpposite;
        b.queuedOpposite = false;
        b.waitTicks = -1;
    } else if (b.waitTicks > 0) {
        b.waitTicks--;
    } else if (b.fireOppositeNext) {
        fire(pl, !b.bufferedPush);
        b.fireOppositeNext = false;
    } else if (b.gameHold != b.physicalHold) {
        // Seguro: el juego siempre termina igual que tu botón real.
        fire(pl, b.physicalHold);
    }
}

} // namespace

// ---------------- Hooks ----------------

class $modify(CFPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        b.ghost1 = nullptr;
        b.ghost2 = nullptr;
        b.resetAll();
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        b.ghost1 = makeGhost(this);
        b.ghost2 = makeGhost(this);
        return true;
    }

    void resetLevel() {
        if (!b.simulating) {
            b.clearPending();
            b.gameHold = false;
            b.spamStreak = 0;
        }
        PlayLayer::resetLevel();
    }

    void onQuit() {
        b.ghost1 = nullptr;
        b.ghost2 = nullptr;
        b.resetAll();
        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (b.simulating && (player == b.ghost1 || player == b.ghost2)) {
            b.simDied = true;
            return;
        }
        if (!b.simulating) {   // muerte real: limpiar el buffer
            b.clearPending();
            b.gameHold = false;
            b.spamStreak = 0;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void playEndAnimationToPos(CCPoint pos) {
        if (b.simulating) return;
        PlayLayer::playEndAnimationToPos(pos);
    }
};

class $modify(CFEditor, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool noUI) {
        b.ghost1 = nullptr;
        b.ghost2 = nullptr;
        b.resetAll();
        return LevelEditorLayer::init(level, noUI);
    }
};

class $modify(CFBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this && !b.simulating && !b.firing && button == JUMP) {
            if (!intercept(pl, down, isPlayer1)) return;
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void collisionCheckObjects(PlayerObject* player, std::vector<GameObject*>* objects, int count, float dt) {
        if (b.simulating) {
            std::vector<GameObject*> keep;
            for (int i = 0; i < count; ++i) {
                auto o = objects->at(i);
                if (!o) continue;
                auto t = o->m_objectType;
                if (t == GameObjectType::Solid || t == GameObjectType::Hazard ||
                    t == GameObjectType::AnimatedHazard || t == GameObjectType::CollisionObject ||
                    t == GameObjectType::Slope) {
                    keep.push_back(o);
                }
            }
            GJBaseGameLayer::collisionCheckObjects(player, &keep, static_cast<int>(keep.size()), dt);
            return;
        }
        GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
    }

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* obj) {
        if (b.simulating) return false;
        return GJBaseGameLayer::canBeActivatedByPlayer(player, obj);
    }

    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        if (b.simulating) return;
        GJBaseGameLayer::flipGravity(player, flip, noEffects);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (b.simulating) return;
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }
};

class $modify(CFPlayerObject, PlayerObject) {
    void update(float dt) {
        auto pl = PlayLayer::get();
        bool realP1 = pl && !b.simulating && this == pl->m_player1;
        if (realP1) onTick(pl);
        PlayerObject::update(dt);
        if (realP1) {
            float warp = pl->m_gameState.m_timeWarp;
            b.tickDt = warp > 0.f ? dt / warp : dt;
        }
    }

    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (b.simulating) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }

    void incrementJumps() {
        if (b.simulating) return;
        PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* ring, bool skipCheck) {
        if (b.simulating) return;
        PlayerObject::ringJump(ring, skipCheck);
    }
};

class $modify(CFHardStreak, HardStreak) {
    void addPoint(CCPoint p) {
        if (b.simulating) return;
        HardStreak::addPoint(p);
    }
};

class $modify(CFEffectGameObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, std::vector<int> const* remapKeys) {
        if (b.simulating) return;
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};

class $modify(CFGameObject, GameObject) {
    void playShineEffect() {
        if (b.simulating) return;
        GameObject::playShineEffect();
    }
};
