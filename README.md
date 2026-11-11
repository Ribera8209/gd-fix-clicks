# Click Fix — mod de Geometry Dash

Corrige el timing de tus clics en **todos los vehículos** (cubo, nave, bola, UFO, wave, robot, araña, swing).
Cuando vas a morir por un clic un poco temprano o tardío, el mod rebobina unos frames, busca el momento correcto para ese clic y lo aplica.

## Cómo funciona
1. Guarda el estado del nivel en cada frame (buffer corto).
2. Registra cuándo presionas y sueltas.
3. Si vas a morir y diste clic hace poco → prueba mover ese clic ±1, ±2… frames.
4. Si con algún ajuste sobrevives (y sigues vivo unos frames más), lo aplica. Si no, mueres normal.

Sólo corrige el **último** clic y sólo dentro de la ventana configurada. No juega por ti si no das clic.

## Ajustes (en Geode → Click Fix → ⚙️)
- **Activado**
- **Ventana de corrección** (default 6 frames)
- **Verificación hacia adelante** (default 20 frames)

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
