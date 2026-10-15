# OnceMore manual

OnceMore makes your guitar sound as if it had been recorded to tape, copied a
few times and played back on a tired machine. One end of its range is a
gentle wow that blurs chords with a warm, slightly seasick drift; the other
is a narrow, noisy, broken tape that drops out, drags and stops.

It runs on the PedalPCB Terrarium with a Daisy Seed. Mono in, mono out, 48 kHz.

## Panel

```
   Wow        Flutter     Saturate
   Model      Failure     Mix

   Hiss   Hum   Static   FS2 hold
   (up = on)             (up = tape stop, down = Fail)

   LED 1                  LED 2
   FS1                    FS2
```

### Knobs

| Knob | What it does | At noon | Default |
| --- | --- | --- | --- |
| **Wow** | Slow pitch drift (0.3–2 Hz), like a worn capstan or stretched tape. Fully clockwise is about ±50 cents. Turn to zero for none. | about ±27 cents | about half past 9 (±14 cents) |
| **Flutter** | Fast pitch flutter (6–20 Hz) that also wobbles the volume a little. Fully clockwise is about ±30 cents. | about ±16 cents | about half past 8 (±5 cents) |
| **Saturate** | Tape drive. The level stays roughly the same as you turn it; only the colour changes, from clean warmth to dense compression. | drive 3.5 | drive 3, just before noon |
| **Model** | The machine. Eight positions, clean to broken (see below). | 4 or 5 | 2, cassette (about half past 8) |
| **Failure** | How broken the tape and transport are: dropouts, drags, crinkles, and near the top, tape stops. | light | 0, none |
| **Mix** | Dry/wet. Dry and wet are time-aligned, so partial settings blend cleanly. | 50% | 100% wet |

### Machines (Model knob, counter-clockwise to clockwise)

| # | Machine | Bandwidth | Hiss | Character |
| --- | --- | --- | --- | --- |
| 1 | Reel-to-reel | 30 Hz – 18 kHz | almost none | clean, warm |
| 2 | Cassette | 40 Hz – 12 kHz | quiet | the classic cassette sound |
| 3 | Walkman | 60 Hz – 9 kHz | moderate | dull, thin lows |
| 4 | VHS (SP) | 100 Hz – 10 kHz | moderate | pushed mids |
| 5 | VHS (EP) | 100 Hz – 5 kHz | plain | muffled, smeared |
| 6 | Dictaphone | 300 Hz – 5 kHz | plain | narrow, thin |
| 7 | Digital answering machine | 300 Hz – 3.4 kHz | none | 8 kHz / 8-bit grit |
| 8 | Toy recorder | 400 Hz – 2 kHz | none | 4 kHz / 4-bit, the most broken |

Changing the machine crossfades over 30 ms, so you can turn Model while
playing. Wow and flutter come from their own knobs on every machine,
digital ones included. The digital machines follow your playing level, so
their grit stays in proportion to the guitar instead of burying it.

### Toggles

| Toggle | Up | Down | Default |
| --- | --- | --- | --- |
| 1 **Hiss** | tape hiss, at the machine's own level | off | up |
| 2 **Hum** | 60 Hz mains hum with its harmonics | off | down |
| 3 **Static** | the machine's own noises: a head-switching buzz that drifts in and out, a motor whine that bends with the pitch, crackles | off | down |
| 4 **FS2 hold** | holding FS2 stops the tape | holding FS2 is Fail | up |

Noise levels are set like real machines against a guitar: hiss you notice
when you stop playing, hum and static clearly there but under the guitar.

### Footswitches

| | Tap | Hold | Let go |
| --- | --- | --- | --- |
| **FS1** | bypass on/off | **Warp**: wow and flutter rise to the maximum | they return to the knobs |
| **FS2** | **Drag**: the tape slows for a moment and catches up | Toggle 4 up: **tape stop**, the pitch slides down to silence. Down: **Fail**, Failure pushed to the maximum | the tape spins back up, or Failure returns to its knob |

A tap acts when you lift your foot. A hold starts after half a second.
Warp and Fail take 0.2 s to arrive and to leave; the tape stop takes 0.6 s.
In bypass the output is exactly the input; tape has no tails, so nothing
rings on.

### LEDs

- **LED 1**: on while the effect is on. With Wow up, it breathes with the
  pitch drift.
- **LED 2**: blinks at every dropout, transport fault or crinkle; dims with
  the tape speed during a stop; stays on while Fail is held.

## Failure in more detail

The first half of the knob is for character, the second half for falling
apart. (Clock positions assume a pot that runs from 7 to 5 o'clock.)

| Failure | Dropouts | Drags | Crinkles | Tape stops |
| --- | --- | --- | --- | --- |
| 0 | none | none | none | none |
| noon | about one every 4 s, shallow | about one every 8 s, slight | about one every 2 s | none |
| full | 2 a second, up to 30 dB deep | 0.5 a second, up to 6% slower | 2 a second | past about half past 3, now and then |

A **dropout** is a patch of worn oxide: the level dips and the top end goes
first. A **drag** is the transport catching: the pitch sags, then runs a
little fast while it catches up. A **crinkle** is a short click of creased tape.
A **tape stop** slides to silence, waits a moment, and starts again.

The FS2 tap drag is always at least a clearly audible one, whatever the
Failure knob says.

## Settings to start from

| Sound | Wow | Flutter | Saturate | Model | Failure | Mix | Toggles |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Warm cassette | 8 o'clock | 8 o'clock | 9 o'clock | 2 | 0 | 100% | Hiss up |
| Seasick chords | 2 o'clock | 9 o'clock | 9 o'clock | 1 | 0 | 60% | Hiss up |
| Old VHS | 10 o'clock | 10 o'clock | noon | 5 | 9 o'clock | 100% | Hiss, Static up |
| Answering machine | 9 o'clock | 9 o'clock | 10 o'clock | 7 | 10 o'clock | 100% | Hum up |
| Broken toy | noon | noon | 2 o'clock | 8 | 3 o'clock | 100% | all up |

## Hidden settings

These have no knob on the pedal yet: on the pedal they stay at their
defaults. You can hear them with the desktop renderer fx-run (see the README,
"Build from source") by setting them in a control script, e.g.
`- { t: 0, param: model_override, to: 0.9 }` (values are 0–1 across the
range). A hidden panel layer may reach them on the pedal in a later version.

| Setting | Range | Default |
| --- | --- | --- |
| Model override | 0 = follow the knob; 1–10 pick a machine, including 9 (chrome cassette, brighter) and 10 (cassette at half speed, darker) | 0 |
| Bias | 0–0.1: asymmetry in the saturation, a fuller tone | 0.03 |
| Hiss level | −24 to +18 dB around the machine's hiss | 0 dB |
| Hum level | −90 to −50 dBFS | −66 dBFS |
| Hum freq | 50 or 60 Hz | 60 Hz |
| Static level | −24 to +18 dB | 0 dB |
| Flutter AM | how much flutter moves the volume, 0–1 | 0.5 |
| Dropouts, Snags, Crinkles | switch each kind of failure on or off | on |
| Performance speed | how fast Warp and Fail arrive and leave, 100–2000 ms | 200 ms |
| Digital depth | how far machines 7 and 8 go, 0 = clean | 1 |
| Digital filter | off: no band-limiting before the sample rate drops, harsher aliasing | on |
| Dry align | off: dry is instant, but partial Mix sounds hollow | on |
| Output level | −12 to +6 dB | 0 dB |

## Specifications

- Mono, 48 kHz on the pedal; latency through the wet path about 5 ms
- Pedal CPU load about 29% average on a Daisy Seed 1.1
- Randomness is seeded: the same input and settings give the same output
  in the offline host, which is how every release is checked
