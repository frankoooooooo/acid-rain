# Acid Rain

A TB-303 voice for the OP-XY, an [Open-XY](https://github.com/etcetc000/open-xy)
machine (ID `acidrain`). The sound is Robin Schmidt's
[Open303](https://github.com/RobinSchmidt/Open303) in fixed point. The screen is
a round smiley painted on the floor under water: every note drops into the
water: a glitchy, tilted raindrop falls onto the surface and its uneven rings
refract the floor and the face a few pixels. At rest the face
and the grid are still and straight.

A few big controls change the whole line, as Sting does: the engine can't write
notes, so it generates the lanes around them (accent, slide, octave and gate),
decided once per note and the same every time the pattern loops.

## Controls

Press any encoder to switch pages.

| Page | Control | Shows | Default | What it does |
| --- | --- | --- | --- | --- |
| 1 | cutoff | `220` to `5.6k` Hz | `803` | The filter's cutoff. On screen: low gives broad slow swells, high gives tight rings |
| 1 | reso | 0 to 100 | 40 | Resonance. On screen: how many rings each drop carries |
| 1 | env mod | 0 to 100 | 50 | How far the filter envelope opens the cutoff. On screen: the rings' height |
| 1 | decay | `0.2s` to `2.0s` | `0.4s` | The filter envelope's decay. On screen: how long ripples last |
| 2 | accent | `off`, `on`, `auto` | off | `on` accents every note; `auto` accents the notes it rains on. The eyes' outlines morph into bolts |
| 2 | slide | `off`, `on`, `auto` | off | `on` glides into this note; `auto` lets the drizzle decide slides, octave jumps and short gates. The lane shows each slide as a bar |
| 2 | ph | `2.0` to `7.0` | `7.0` | Turn anticlockwise for acid: more auto slides and octave jumps, and corrosion (wavefold and grit) that deepens over consecutive accents. 7.0 is clean. On screen: the face's line pits where the water moves it |
| 2 | dew | 0 to 100 | 0 | How often it rains. 0 never rains. On screen: a splash and bolt eyes on each rain accent |

Accent and slide are meant to be **parameter-locked per step**: a lock of `on`
or `off` always wins, and `auto` hands that step to the rain.

- **Slide means "glide into this note".** A lock lasts only its own step, so
  lock slide on the step you glide *to*. The pitch glides over about 60 ms
  from the previous note and the filter envelope carries on without retriggering.
- **Accent behaves as on a 303**: a shorter, stronger filter sweep, a louder
  note, and a sweep that builds over consecutive accents.
- **The rain** depends only on when notes start, on pH and on dew: each note
  adds humidity, which dries out with time. Dense playing builds it up; when it
  crosses the dew threshold it rains, accenting the next notes on `auto` (two
  in a row under pH 4.5). The screen runs the same model, so its splashes land
  on the notes you hear accented.
- **Monophonic slides**: a slide closes the note it replaces within about 5 ms,
  so the glide carries the line alone even on a polyphonic track.
- **Body**: as resonance rises, the band under 150 Hz is added back (up to
  +4 dB at full resonance), so the squelch doesn't thin the bass.
- **The drizzle** (slide on `auto`): each note also adds to a drizzle store
  that dries with time. When it crosses a threshold set by pH, that note
  slides in: dense playing slides more, gaps dry it out, and at pH 7 only
  dense runs slide. Under pH 4.5 every other slide also jumps an octave, and
  under pH 5.5 the note after a slide closes early. Like the rain, it depends
  only on note timing, pH and dew, so the lane shows exactly the slides
  (bars), octave jumps (squares above) and short gates (dim cells) you hear.

Env mod and decay are an envelope on the engine page, which the OP-XY's UX
guide normally leaves to the track's own envelopes. They stay because the 303's
filter envelope, and how accent plays against it, are the voice's own
character, not a general-purpose envelope.

**Defaults are clean**: no accents, no slides, no corrosion and no rain. The
presets show those off. Leave the track's own filter fully open, as the
presets do, and use mono play mode.

### Limits

- Accent, slide and the rain share one state across every track that plays
  Acid Rain, so slide and rain follow one line reliably on one track only.
- The engine only counts time while it renders. After a rest its humidity and
  drizzle can differ from the screen's, which counts real time, so the lane
  and splashes can drift from the audio until the stores settle again.

## Presets

`squelch`, `drizzle`, `downpour`, `corroded`, `glider` and `monsoon`, from
clean to acid and from dry to monsoon. They live in `presets/` (written from
`definition.json` by `tools/make_presets.py`).

## Develop

Keep the [SDK](https://github.com/etcetc000/open-xy) beside this repository as
`../open-xy`, on `staging`.

```sh
python ../open-xy/sdk.py test --machine .
python ../open-xy/sdk.py preview --machine . --out ../previews/acidrain-001
python ../open-xy/tools/serve_harness.py --preview ../previews/acidrain-001
```

The tables and artwork are generated; edit the scripts and run them again:

- `tools/make_tables.py` writes `tables.h` (the voice) and `rain_tables.h` (the humidity model).
- `tools/make_panel.py` writes `panel.svg.fragment` and `display_tables.h`, so
  the still grid and the moving one share one geometry. `OUTLINE`, `MOUTH`,
  `EYES` (line weights, px) and `ROTATION` (the face turned on the floor) set the face; `square_wave` in `engine.cpp` swaps the saw
  for Open303's square, and `sub_level` (0, off) mixes in a clean sine an
  octave below the played note.
- `tools/make_presets.py` writes `presets/`.

`tests/sound.cpp` checks the headroom at every corner (with stacked accents and
the lowest notes), accent, slide, late locks, rain and silence.
`tests/display.cpp` checks that every frame is valid, that the screen is still
at rest and that the redraw stays near 20 nodes a frame. [REPORT.md](REPORT.md)
has the measurements.

## Licence

MIT (see [LICENSE](LICENSE)). The DSP is derived from Open303, Copyright (c)
2009 Robin Schmidt, under the MIT License; its notice is in [NOTICE](NOTICE).
