# 0.6 previews

Small scenes for checking the 0.6 random features by eye. Render one with
`ipc` and `ipgen`, then zoom in with nearest-neighbour scaling:

```sh
build/tools/ipc examples/previews/metal-gradients.ips -o metal.iprop
build/tools/ipgen metal.iprop --seed 7 -o metal.png
```

- `metal-gradients.ips` — one iron-to-steel colour gradient drawn uniformly,
  with a gaussian centred on the middle and with a beta leaning to the bright
  end, each with a density strip of the drawn positions.
- `parameters.ips` — stroke widths and colour corrections for the same ten
  seeds under a uniform, a gaussian and a triangular distribution. A column
  keeps its rank in every row, because each draw maps the same stream step
  through the distribution's inverse CDF.
- `segments.ips` — points of a `vec2` segment, uniform and gaussian, beside an
  independent `{x0 to x1, y0 to y1}` box and the diagonal of
  `{0.0, 0.0} to {1.0, 1.0}`.

The nested-parts preview is the mushroom colony in [`../colony`](../colony).
