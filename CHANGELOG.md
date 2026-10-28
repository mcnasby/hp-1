# Changelog

A release is a push to `main` that changes `VERSION` to a version with no tag yet. CI then makes the
release build, tags the commit `vX.Y.Z` and publishes the GitHub Release with that version's notes,
taken from its section below. Add the section in the same pull request as the `VERSION` change.

## 0.4.1

- **The splash sounds like a cymbal slapped by hand.** It's now a soft wash from 500 Hz to 16 kHz that
  blooms over a few tens of milliseconds, with no stick click and no ringing "rod" tones. The highs fade
  first and the mids carry the tail, with a dull pat of the palm underneath.
- **The shakers are egg shakers.** They're high and airy, nearly all 6 to 16 kHz. The left key is the
  push stroke, which swells a little slower and lasts longer; the right is the pull, a quicker snap.
- Both are shaped against recordings of real cymbals and egg shakers.
- **A tidier pan on the screen.** Every note now sits in one evenly spaced ring round the ding,
  instead of a lone extra note squeezed in near the rim. The second pan's fields have more room, and show their notes as the first pan's do.
- **The black-key strip is always above the pan**, as the black keys are above the white.

## 0.4.0

- **Thwap and tak moved to the white keys.** The end white key on each side is a palm thwap and the
  next one in is a fingertip tak. The twelve white keys between play the pan.
- **A kit on the black keys**, with four new sounds: **castanets** on the far left, a **shaker** (a
  cabasa) on the second key from each end (pushed on the left, pulled back on the right, so you can
  alternate them for a shuffle), and a **splash cymbal** on the far right. The pan's own hits (Gu,
  ghost, ding slap, tak, harmonic, rim, mute) stay in the middle.
- **A bigger second pan.** With Black on Duo, the castanets stay on the far left and the Gu is on the
  far right. The nine keys between are the second pan: its ding and eight fields, ding in the middle.
- **The second pan sits in the first pan's register.** Its ding is where a pan in its key has it: D
  Kurd's D Celtic now starts on D3, not D4.
- **The black-key strip sits above the pans in Duo**, the way the black keys sit above the white keys.
  Without Duo, the strip stays at the bottom.
- **Higher notes sound more like a real pan.** On small, high fields the octave is a little louder and
  flatter, and the fifth is weaker, shorter and slightly off. The field's own untuned overtone comes
  in, and the neighboring notes ring along. The strike is a touch harder and brighter. Low notes are
  unchanged. Each metal sets how much: Stainless the most, Ember the least. Checked against Eyal Alon's
  measurements of real pans and recordings of handpan players.
- MIDI channel 10 now plays the kit too: crash or splash (49, 55, 57), cabasa (69), maracas (70),
  castanets (85).

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
