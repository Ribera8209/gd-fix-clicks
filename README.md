# Afterflow — port a Geometry Dash 2.2091 (Geode v6)

Port **no oficial** de [Afterflow](https://github.com/afterflow-gd/afterflow) por **prsevend**, adaptado a GD 2.2091 / Geode 6.0.0-alpha.1.
Licencia MIT (ver `LICENSE`). Todo el crédito del mod es de su autor original.

Cambios del port:
- `mod.json`: Geode 6.0.0-alpha.1, GD 2.2091.
- `PlayLayer::init` con la firma nueva de 2.209x.
- `gd::vector` → `std::vector` (Geode 6 los eliminó).
- Actualizador automático desactivado (descargaría la versión para 2.2081).

Ver `about.md` para cómo funciona y sus controles (F6 cambia de etapa, F9 abre el panel).
