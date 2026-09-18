# FACE FIGHT: Chi vs You

## One-line pitch

The camera captures the player's face and turns it into the boss: repeatedly tap
the face to make Chi attack, and press and hold Chi to guard incoming attacks.

## Core loop

1. `FIND FACE`: only the live camera and framing guide are shown while the
   detector acquires one primary player. Chi stays hidden to avoid obscuring the
   face-alignment task.
2. `FACE READY`: a dedicated two-stage face model supplies the face box. A
   feathered head silhouette removes the square camera surroundings and the
   portrait is kept stable for the whole round.
3. Tap the captured face to start. The camera feed is then replaced by the
   office/rooftop arena, alternating each round.
4. `FIGHT`: the face boss and Chi each start with 100 HP. Chi's default combat
   pose is side-on toward the boss, not the front-facing creature idle.
5. Tapping the moving face starts Chi's next punch/kick and damages the boss.
6. The boss bobs while idle, flashes a warning, then visibly lunges at Chi.
7. Holding a finger on Chi at impact blocks the attack; releasing ends the guard.
8. Zero boss HP is a Chi KO victory; zero Chi HP is defeat.
9. Tap the result screen to scan a new challenger and replay.

## Input contract

- Repeated taps inside the face portrait: one attack per distinct press.
- Press and hold inside Chi: guard remains active only while the touch remains
  down and within Chi's hit area.
- Sound peak while the special meter is full: roundhouse special.
- Touches outside the face and Chi have no combat effect.

## Combat rules

- Punch: 7 damage. Every fifth valid face tap uses kick for 12 damage.
- Combo expires after 1.2 seconds without a valid face tap.
- Boss warning lasts 750 ms and is deliberately visible before impact.
- Unblocked boss hit: 14 damage. Blocked hit: 2 chip damage.
- Boss attack interval becomes shorter below 50 HP.
- Attacks and guard are mutually exclusive.

## State machine

```
FIND_FACE -> FACE_READY -> FIGHT -> CHI_WINS
     ^                       |  \-> FACE_WINS
     +-------- tap ----------+------- tap ----+
```

Within `FIGHT`, the boss cycles `IDLE -> WARNING -> STRIKE -> IDLE`. Chi cycles
`IDLE -> ATTACK -> IDLE` or `IDLE -> GUARD (held) -> IDLE`.

## Failure behaviour

- No face detected: remain in live `FIND YOUR FACE`; never start with a body crop.
- Detection flicker after capture: keep the portrait and continue the round.
- Invalid/too-small player box: do not capture; keep scanning.
- Touch controller read failure: current press is cleared, preventing a stuck guard.
- Camera frame failure during a round: retain the portrait and combat state.

## Deliberately out of scope

- Identity recognition, saving faces, networking, SD recording, multiple players,
  pose recognition, menus, and persistent high scores.

## Acceptance criteria

- A stable portrait is captured only from the dedicated face detector.
- The portrait has a feathered transparent head silhouette, not a square photo.
- Status text makes scanning, capture, ready, warning, KO and retry states explicit.
- During combat Chi waits in a right-facing side profile and the face boss moves.
- Face tapping visibly triggers attacks and reduces boss HP.
- Holding Chi visibly guards; releasing visibly cancels guard.
- Both victory and defeat are reachable and replayable.
- The firmware builds within the current flash and PSRAM budget.
