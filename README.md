# Click Fix — mod de Geometry Dash

Corrige el timing de tus clics en **todos los vehículos** (cubo, nave, bola, UFO, wave, robot, araña, swing).
Cuando vas a morir por un clic un poco temprano o tardío, el mod rebobina unos frames, busca el momento correcto para ese clic y lo aplica.

## Cómo funciona (v0.4, Predictive Input Buffer)
1. Al dar clic, una copia invisible del jugador simula el salto con la física real (sólo choca con bloques, pinchos y rampas).
2. Si saltar ahora sobrevive, el clic pasa normal.
3. Si choca, busca el primer **tick** (1/240 s) en que el salto sí pasa y lo da exactamente ahí.
4. El nivel y el jugador real nunca se regresan: sin teletransporte.

Detalles:
- Nave: no se toca.
- Soltar sólo se corrige en wave y robot.
- Si spameas la wave (3+ clics muy rápidos), no retrasa.
- Si mueres, el buffer se limpia.
- Un clic tardío no se puede corregir. Cerca de orbes, pads y portales puede fallar.

Ideas basadas en "Show Trajectory" (Eclipse Menu) y en los mods "Predictive Input Buffer" de la comunidad. Implementación propia.

## Ajustes (en Geode → Click Fix → ⚙️)
- **Activado**
- **Retraso máximo** (default 4 ticks = 1 frame a 60 FPS)
- **Retraso máximo en wave** (default 4 ticks)

## Compilar (opción fácil, sin instalar nada)
1. Crea un repositorio en GitHub y sube esta carpeta.
2. Ve a la pestaña **Actions** → se compila solo.
3. Descarga el artefacto `ClickFix-geode` → adentro está `shaq.click-fix.geode`.
4. Cópialo a la carpeta `geode/mods` de tu Geometry Dash (necesitas Geode instalado).

## Compilar local
Requiere Geode SDK + CLI (https://docs.geode-sdk.org):
```
geode build
```

## Versiones
- **Geometry Dash 2.209x (2.2091)** — si tienes 2.209 sin actualizar, actualiza el juego.
- **Geode v6.0.0-alpha.1** (el que soporta 2.209x). Instálalo desde geode-sdk.org.

## Notas
- Versión experimental 0.1: no se probó en el juego. Si falla al compilar, casi siempre es un nombre de función/campo que cambió entre versiones de GD.
- Puede haber un micro-tirón en el frame donde corrige, y algún sonido duplicado.
- Usarlo para verificar niveles o subir récords se considera hack en la comunidad de GD.
