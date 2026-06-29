# Tycho branding

An abstract take on the **Tychonic system** — Tycho Brahe's model of nested
orbits. The geometry doubles as the language's memory model:

- each **ring** is a scope boundary (a nested arena);
- the **centre** dot is the root arena (`main`);
- the **dots on rings** are values;
- the off-centre **satellite with its own ring** is a nested scope carrying its
  own arena hierarchy.

## Files

| File | Use |
| --- | --- |
| `tycho-logo.svg` | primary mark, dark ink — for light backgrounds |
| `tycho-logo-dark.svg` | light ink — for dark backgrounds |
| `tycho-favicon.svg` | simplified mark for small sizes; adapts to the browser theme via an embedded `prefers-color-scheme` query |

All are SVG (scale infinitely), transparent background. Ink is `#1c1c2e`
(deep indigo) on light, `#ececf4` on dark — recolour by editing the one hex.

PNG/ICO exports aren't checked in; rasterise them as needed, e.g.
`rsvg-convert -w 512 tycho-logo.svg > tycho-logo-512.png`.
