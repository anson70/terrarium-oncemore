# OnceMore golden files

Reference outputs a human has listened to and approved; a golden file is
only made after that. CTest renders
the tour again and requires the output to be bit-identical, so any change
that alters an approved sound fails until it is listened to again.

| File | Approved | Made with (from the repository root) |
| --- | --- | --- |
| `tour.wav` | 2026-10-14, Anson (fx-live), OnceMore v1.0 | `fx-run oncemore --signal sweep:100:2000:-6 --duration 10 --seed 5 --script effects/oncemore/tests/tour.yaml` |

The arguments live in `../../effect.cmake` (`FX_TOUR_ARGS`); keep the two in step.

The comparison runs on Linux only: float `sin`, `exp` and `pow` come from
the platform's maths library, and MSVC's and Apple's round a few results
differently from glibc's, which is enough to move a random event by a sample.
GCC and Clang, Release and Debug, all reproduce these files exactly on Linux.

After an intended change to the sound: listen in fx-live, then regenerate
with the command above plus `--out effects/oncemore/tests/golden/tour.wav`
and update this table.
