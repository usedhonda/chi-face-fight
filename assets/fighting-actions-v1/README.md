# Chi fighting animation library v1

All final frames are transparent RGBA8888, normalized to 80x96 with one common scale per action and a shared ground row. The firmware renders them at 100x120.

| Action | Frames | Final raw asset | Firmware timing |
|---|---:|---|---:|
| Punch combo | 6 | `punch-combo-final/chi-punch-combo-v1.rgba` | 100 ms/frame |
| Block | 4 | `block-final/chi-block-v1.rgba` | 150 ms/frame |
| Dodge | 4 | `dodge-final/chi-dodge-v1.rgba` | 120 ms/frame |
| Jump | 5 | `jump-final/chi-jump-v1.rgba` | 130 ms/frame |
| Roundhouse | 6 | `roundhouse-final-v2/chi-roundhouse-v2.rgba` | 100 ms/frame |
| Knockdown | 6 | `knockdown-final/chi-knockdown-v1.rgba` | 140 ms/frame |
| Get up | 5 | `get-up-final/chi-get-up-v1.rgba` | 130 ms/frame |
| Defeat | 4 | `defeat-final/chi-defeat-v1.rgba` | 180 ms/frame plus 1 s hold |

Existing `game-actions-v1` supplies Kick, Hit, and Victory. Existing ClawGate-derived assets supply Idle and four-direction Walk.

## Touch showcase

- Tap Chi repeatedly: Punch Combo -> Block -> Dodge -> Jump -> Roundhouse -> Hit -> Knockdown/Get Up -> Defeat -> Victory -> repeat.
- Tap outside Chi: move to the point -> Kick -> Victory -> Idle.
- Dodge and Jump use temporary firmware draw offsets; the persistent character position does not change.

## Generation contract

Built-in image generation used the current Chi idle image as the authoritative identity reference and the approved kick/hit/victory sheets as the game-sprite rendering reference. Each prompt fixed character identity, black bob haircut, navy blazer, white shirt, black skirt, leg and shoe colors, head/body scale, line weight, transparent background, exact frame count, full-body framing, coherent sequential motion, and prohibited text, grids, weapons, detached parts, extra limbs, zoom, motion blur, and background art.

`../prepare_animation.py --sheet-input` extracted left-to-right characters, removed unrelated connected components, rejected ambiguous frame counts, and created the final PNG frames, atlas, RGBA file, contact sheet, and metrics JSON.
