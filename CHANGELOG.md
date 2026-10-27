# Changelog

A release is a push to `main` that changes `VERSION` to a version with no tag yet. CI then makes the
release build, tags the commit `vX.Y.Z` and publishes the GitHub Release with that version's notes,
taken from its section below. Add the section in the same pull request as the `VERSION` change.

## 0.3.0

- **The Pan layout now matches a real pan.** The first field after the ding is on the right, then the
  left, right, left, as makers lay them out: a D Kurd's A3 is just right of the ding, Bb3 just left,
  C4 right, D4 left, up to C5. It was the other way round. The second pan's keys follow.
- **Pan rev** (Layout) keeps the old way, left first, for those who play it that way. (Mirror is gone:
  Pan rev is what it was, and Pan is now the maker's layout.)
- **Kurd has its ninth field**, C5: D3 / A Bb C D E F G A C, the usual Kurd 10.
- **Every note on the pan:** the screen draws all sixteen notes the white keys play on the pan, the
  pan's own fields evenly round the ding and the extra ones in a ring near the rim. The drawing
  follows the layout. The row of extra notes under the pan is gone.

## 0.2.1

- **The second pan is laid out like the first.** With Black on Duo, its ding is now on the middle of
  the seven keys (black key 6) and its notes alternate outward, left then right, as on the white keys.
  It follows Layout: Mirror goes right first, Linear runs low to high. Its ding's key glows dimly.
- **A new scale, SaByeD**, Colin Foulke's, in E as he builds it (E3 / A B C# D# E F# G# B): major,
  with its fourth tucked below the octave. It sits after Mixolydian: one step back from Kurd on
  ALGORITHM.
- **The second pan's fields sit evenly round its ding** on the screen, as the first pan's do.
- **Note names spelled as each scale spells them:** an E pan shows G# and D# (not Ab and Eb), a D Kurd
  still shows Bb. On the screen and on the web page's keys.

## 0.2.0

- **It sounds like a handpan now, not an electric piano.** The tone fields were rebuilt from
  measurements of real pans:
  - The octave and fifth **shimmer**, pulsing at 3–9 Hz the way a real pan's do. Each field has its
    own rate.
  - Notes ring for **about 3 seconds**, like a real pan (the ding longer, the high fields shorter),
    instead of an electric piano's long, even ring.
  - The **octave is nearly as loud as the fundamental** and often outlasts it. A faint double octave
    sits above the fifth.
  - Each strike starts with a short **burst of skin on steel**, more of it the firmer the Strike.
  - The **ding sounds faintly under every note**, through the air inside the pan.
  - A **metallic attack**: each strike rings the field's higher, inharmonic modes for a moment, the
    bright "tang" at the front of a struck pan.
  - **The air**: each note leaves a halo of its harmonics hanging in the pan for a couple of seconds,
    always in tune with the scale. Sympathy sets how much (now 50 by default). A palm mute silences it.
- The metals' character changes with the shimmer: Raw pulses quickest, Ember gentlest.
- **Worn** also widens the shimmer.

## 0.1.0

The first HP-1: FiMba-1's kalimba made into a handpan.

- **Tone fields** tuned like a maker's: each note rings with its octave and compound fifth, a slow
  beat from the field's two near-twin modes, and a touch of steel in the strike.
- **Four metals:** Nitrided, Stainless, Ember and Raw steel. **Size** (44–62 cm) and **Thickness**
  (1.0–1.4 mm) reshape the shell, the air inside and the sustain.
- **The steel:** the two black keys at each end thwap and tak the shoulders, each on its own side.
  The seven between are a kit (Gu, ghost, ding slap, tak, harmonic, rim, mute), or a **second
  handpan** whose scale fits with the first.
- **Sympathy:** a struck note wakes the fields whose partials it meets, so the pan rings along.
- **Ten scales** from the makers' lists (Kurd, Celtic, Integral, Pygmy, Hijaz, Aegean, Equinox,
  Akebono, Major, Mixolydian), each in its usual key and movable to any key.
- **Pan, Mirror and Linear** layouts: the ding on the middle keys, the fields zigzagging outward.
- **Grooves** on ARP: Heartbeat, Cascade, 3 over 2, Backbeat.
- **Humanize** for the FM-1's velocity-less keys; MIDI velocity, and the steel on channel 10.
- **The screen:** the pan from above, fields glowing and rippling as they ring, the rim lighting where
  a thwap lands. Dark by default, Light on the Setup page.
- **The browser version** labels every key with what it plays and the computer key that plays it.
- Kept from FiMba-1: the reverb, delay, grain cloud, tape, lo-fi, chorus and filter, MIDI, MIDI clock,
  autosave, reset to the defaults, and the web installer.
