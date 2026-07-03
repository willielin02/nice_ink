# Nice Ink - Technical Specification

## Game Overview

- **Genre:** 4-6 player multiplayer party game
- **Core Mechanic:** Players take turns being tied up and tattooed by others. The victim's soul detaches to observe and guess who the real tattoo artist is (others mimic to confuse). Tattoos are permanent on the character.
- **Platform:** Steam (PC)
- **Engine:** Unreal Engine 5.5 or 5.6
- **Pricing:** $10.0 full price, $6.7 during Early Access
- **Target:** EA release in 3.5-5 months (solo dev)

---

## Architecture Philosophy

- Core logic encapsulated in C++
- Blueprint used only for wiring: connecting C++ exposed inputs/outputs into game flow, UI, and animation
- Minimal Blueprint logic; maximal C++ determinism and testability

---

## Module 1: Tattoo System

### Overview

Custom C++ system built on UE5 Render Target infrastructure. Simulates realistic tattoo needle stippling, not continuous brush painting.

### Core Classes

```
UTattooComponent        — Attached to each character. Owns the Render Target, drives material updates.
UTattooNeedle           — Defines needle type behavior (dot pattern, frequency, spread radius).
UTattooPalette          — A set of ink colors available during a session.
FTattooStroke           — Single dot data: UV position, color, needle type, pressure, timestamp.
FTattooNetData          — Compressed batch of strokes for network replication.
UTattooSubsystem        — World subsystem managing stroke history for late-join replay.
```

### Needle Types

| Type | Real Equivalent | Behavior |
|------|----------------|----------|
| Round Liner (RL) | 1-5 needles in circle | Single precise dot, 80-150 strikes/sec |
| Round Shader (RS) | Loose needle circle | Slightly larger area, semi-transparent, 60-100 strikes/sec |
| Magnum (M1) | Flat row of needles | Rectangular dot pattern, fills fast, 50-80 strikes/sec |
| Curved Magnum | Curved row | Softer gradient edges, for shading |

### Material Integration ("In-Skin" Look)

The tattoo must appear embedded in the dermis, not adhered on top.

**Material blend order (bottom to top):**
1. Subsurface Color: Lerp tattoo RT color into SSS channel (~70-85% opacity)
2. Base Color: Blend tattoo RT into base color below skin micro-detail
3. Normal Map: Skin pores/wrinkles remain ON TOP (unmodified by tattoo)
4. Roughness: Slight decrease in tattoo areas (fresh ink is slightly shinier)
5. Tattoo edge: Micro gaussian blur simulating ink diffusion in dermis

**Implementation:** Custom Material Function (~10-15 nodes) inserted into MetaHuman skin material instance.

### Render Target Setup

- Resolution: 1024x1024 per character (DXT5 ~1.33MB each, 6 players = ~8MB VRAM total)
- Format: RGBA8 (sufficient for ink color + alpha)
- Address mode: Clamped
- One RT per character for paint accumulation
- One RT (shared) for UV unwrap reference (captured once at match start via SceneCapture2D + Pre-Skinned Local Position)

### UV Acquisition (Skeletal Mesh Workaround)

`FindCollisionUV` does NOT work on skeletal meshes (by design, JIRA UE-39787).

**Solution:** SceneCapture2D + Pre-Skinned Local Position method:
1. At match start, capture reference pose world positions into a UV-lookup RT
2. Line trace hits Physics Asset → get approximate world position
3. Sample UV-lookup RT to find corresponding UV coordinate
4. Apply dot at that UV position on the paint accumulation RT

### Multiplayer Sync

- Pattern: Replicate stroke COMMANDS, not texture data
- Client → ServerRPC (Unreliable): `TArray<FTattooStroke>` batch
- Server validates → Multicast RPC to all clients
- Each client renders dots locally on their own RT
- Late-join: Server sends full stroke history via Fast TArray Replication (60x performance vs naive TArray)
- Bandwidth: ~150 dots/sec * 20 bytes = ~3 KB/s (negligible for 4-6 players)

### Persistence (Permanent Tattoos)

- End of match: `FImageUtils::GetRenderTargetImage()` → compress PNG → save locally
- Steam Cloud syncs the PNG file (free, automatic)
- On room join: Load PNG → `UTexture2D` → set as material parameter
- File size: ~200-500KB per character

### Exposed Blueprint Ports

**Inputs:**
- SelectNeedle(ENeedleType)
- SelectColor(FLinearColor)
- StartTattooing(FHitResult)
- StopTattooing()

**Outputs:**
- OnStrokeApplied(FTattooStroke) — event for SFX/VFX feedback
- OnNeedleChanged(ENeedleType)
- GetRenderTarget() → UTextureRenderTarget2D*

---

## Module 2: Character Customization

### Overview

Runtime customization first, MetaHuman base later. Goal: player can make character resemble themselves in ~3-5 minutes without photo upload, editor tools, or a backend generation job.

**Current decision:** Nice Ink's player-facing customization system is built by us. MetaHuman is only the planned realistic character base. MetaHuman Creator / MetaHuman Character Editor is an official developer/editor tool, not the runtime UI that ships to players.

**Non-goals for first in-game flow:**
- Do not embed MetaHuman Creator inside the packaged game.
- Do not depend on photo-to-character generation or backend generation jobs.
- Do not treat MetaHuman as the tattoo, save, sync, or UI system.
- Do not claim a slider is complete unless it visibly affects a real mesh or is clearly marked unbound in debug UI.

### Core Class

```
UCharacterCustomizer     — ActorComponent on player character.
FCharacterAppearance     — Serializable struct holding all customization params.
```

### Customization Parameters

**Required asset foundation:**
- A character base must exist before the system can be called complete. This can be a MetaHuman or a test skeletal mesh with real morph targets.
- Add `UNiceInkCharacterProfile` as the asset/profile layer for face mesh, body mesh, eye materials, skin material slots, hair/brow/facial hair options, available morph targets, morph bindings, and preset data.
- The same runtime UI talks to the profile layer, so replacing a test mesh with MetaHuman should not require rewriting the UI.

**Current runtime control target:**
- Face: 35 player-facing controls grouped by head, brow, eyes, nose, cheeks, mouth, jaw/chin, ears/neck, and detail.
- Body: 12 controls covering height, shoulders, chest, waist, hips, arm/leg proportions, body fat, muscle mass, and posture.
- Body base: body archetype, gender presentation/body base, height in cm or explicit scale mapping, and body preset.
- Appearance: skin tone preset/override, undertone, skin detail, freckles, blemishes, scars, age detail, eye color, hair, brows, facial hair, makeup.
- Facial marks: each mole/freckle/scar/spot stores type, face UV position, size, color, opacity, rotation, and layer order.
- Replication: only `FCharacterAppearance` values replicate; generated mesh assets do not replicate at runtime.

**Implemented C++ face controls:**
- HeadWidth, HeadHeight, FaceRoundness, ForeheadHeight
- BrowHeight, BrowAngle
- EyeSize, EyeSpacing, EyeDepth, EyeAngle, UpperEyelid, LowerEyelid
- NoseBridgeHeight, NoseBridgeWidth, NoseTipSize, NoseTipAngle, NostrilWidth
- CheekboneHeight, CheekboneWidth, CheekFullness
- MouthWidth, UpperLipFullness, LowerLipFullness, MouthCornerHeight
- ChinWidth, ChinHeight, ChinProjection
- JawWidth, JawAngle, JawForward
- EarSize, EarAngle, NeckThickness, AgeLines, FaceAsymmetry

**Implemented C++ body controls:**
- Height, ShoulderWidth, ChestSize, WaistSize, HipWidth
- ArmMuscle, ArmLength, LegMuscle, LegLength
- BodyFat, MuscleMass, Posture

**Face (15-20 morph targets):**
- Jaw width, jaw height, chin protrusion
- Cheekbone height, cheek fullness
- Nose width, nose length, nose bridge height
- Eye size, eye spacing, eye tilt
- Lip fullness, lip width
- Forehead height, brow depth

**Body (3-5 morph targets):**
- Body fat (lean ↔ heavy)
- Muscle mass (soft ↔ muscular)
- Height ratio (short ↔ tall) — via scale, not morph

**Appearance:**
- Skin tone: FLinearColor (hue + saturation + brightness)
- Hair style: 4-6 preset mesh swaps
- Hair color: FLinearColor
- Facial marks: player can pick common positions or click the face preview to place marks freely.
- Starting appearance presets: East Asian, Southeast Asian, South Asian / Indian, Black / African diaspora, White / European, Latino / Latin American, Middle Eastern / North African, Indigenous / Native American, Pacific Islander. These are editable starting points, not locked identity classes.
- Preset blend: optional advanced flow where the player chooses two explicit presets and a ratio, e.g. Preset A 60% + Preset B 40%. Do not use vague "mixed" preset labels.

### Scope Split

**MVP customization:**
- Real character base or test mesh with real morph targets
- Preset selection
- Face sliders that visibly move the mesh
- Skin tone, eye color, hair color
- Hair/brow/facial hair style index swaps
- Body type, height, body fat, muscle mass
- Save/load `FCharacterAppearance`
- Replicate confirmed appearance through PlayerState

**Full customization:**
- Free placement for moles/freckles/scars/spots on the face
- Preset blend between two explicit starting presets
- More skin detail layers: pores, blemishes, scars, aging, makeup
- MetaHuman-specific material forks and LOD handling
- Screenshot-verified preset library covering multiple body bases and skin tones

### UX Flow

1. Select an optional starting appearance preset and body base
2. Adjust face sliders (grouped: eyes, nose, mouth, jaw)
3. Pick skin tone from a gradient picker
4. Choose hair, brows, facial hair, eye color, and makeup
5. Select body type, height, fat, muscle, and posture
6. Place optional facial marks such as moles, freckles, scars, and spots
7. Confirm → serialize to FCharacterAppearance → replicate to all players

### Technical Details

- Runtime customization is implemented with our own C++/UMG layer; MetaHuman is only an asset base.
- MetaHuman LOD: Use LOD2-3 during gameplay (4-6 players), LOD0-1 for tattoo close-ups
- MetaHuman Optimized Export (UE 5.5+): 60MB per character vs 800MB
- Morph targets driven via `USkeletalMeshComponent::SetMorphTarget(FName, float)`
- Skin/eye/hair colors driven through Dynamic Material Instance parameters
- Hair/brow/facial hair driven through groom or mesh option swaps recorded in the character profile
- Facial marks use face UV placement and a face overlay texture/render target layer; they are data-driven, not baked into the base mesh
- All in C++; Blueprint gets a simple "Apply Preset" and individual slider nodes

### Verification Standard

- Every slider in the MVP must have a viewport screenshot proving visible change, or it must be marked as unbound.
- Every preset must be screenshot-tested from front and side views.
- Skin tone, hair color, eye color, body type, and height must round-trip through save/load.
- Two-player PIE must show confirmed appearance replicated to the other client.
- MetaHuman integration is not considered complete until the same tests pass on an imported MetaHuman asset.

### Exposed Blueprint Ports

**Current runtime inputs:**
- ApplyPreset(int32 Index)
- SetFaceControl(ENiceInkFaceControl, float)
- SetBodyControl(ENiceInkBodyControl, float)
- SetFaceMorph(FName, float) for direct/debug overrides
- SetBodyMorph(FName, float) for direct/debug overrides
- SetBodyType(EBodyType)
- SetHeightScale(float)
- SetSkinTone(FLinearColor)
- SetSkinTonePreset(int32)
- SetSkinDetails(int32, float, float, float, float)
- SetSkinUndertone(FLinearColor)
- SetEyeColor(FLinearColor)
- SetHairStyle(int32)
- SetHairColor(FLinearColor)
- SetBrowStyle(int32)
- SetBrowColor(FLinearColor)
- SetFacialHairStyle(int32)
- SetFacialHairColor(FLinearColor)
- SetMakeup(int32, float)
- ConfirmAppearance()

The UI should use `SetFaceControl` and `SetBodyControl` for normal sliders. `FaceMorphBindings` and `BodyMorphBindings` translate those controls to the actual morph target names available on the current character mesh.

**Inputs:**
- ApplyPreset(int32 Index)
- SetFaceMorph(FName, float)
- SetBodyType(EBodyType)
- SetSkinTone(FLinearColor)
- SetHairStyle(int32)
- ConfirmAppearance()

**Outputs:**
- GetAppearanceData() → FCharacterAppearance
- OnAppearanceChanged() — event for UI update

---

## Module 3: Multiplayer Networking

### Technology

- Epic Online Services (EOS) — free, handles NAT traversal, relay, lobby, matchmaking
- Architecture: P2P with host authority (EOS relay fallback)
- Plugin: EOS Integration Kit (EIK) for Blueprint-friendly lobby/matchmaking setup
- Room size: 4-6 players

### Integration

- C++ handles: GameState replication, stroke sync, character appearance sync
- Blueprint handles: Lobby UI, player list display, ready-up flow
- EOS P2P relay auto-handles NAT punchthrough; no dedicated server needed

### Key Replication

| Data | Method | Frequency |
|------|--------|-----------|
| Tattoo strokes | Unreliable Multicast RPC | Per-strike (~80-150/sec during active tattooing) |
| Game phase transitions | Reliable Multicast RPC | On state change |
| Character appearance | RepNotify (FCharacterAppearance) | Once on join |
| Guess attempts | Server RPC → response | On player input |
| Tattoo history (late-join) | Fast TArray Replication | On player connect |

---

## Module 4: Game Flow

### GameMode (C++)

```cpp
enum class ENiceInkPhase : uint8
{
    Lobby,              // Waiting for players, showing characters
    SelectingVictim,    // Vote or random select who gets tattooed
    Binding,            // Binding animation plays
    Tattooing,          // Active tattooing phase (timed, e.g. 15-20 seconds)
    SoulGuessing,       // Victim's soul observes and guesses (every 30 sec)
    Reveal,             // Show who was actually drawing
    Celebration,        // React to result (replays, emotes)
    NextRound           // Rotate roles
};
```

### Soul Perspective (Guessing UX)

- Camera detaches from body, enters observation mode
- Can view all "artists" from a fixed elevated angle (or switchable cameras)
- Each artist's hand/tool is visible but faces are obscured or identical
- Key design challenge: how much synchronization detail is visible
- Guess UI: Select one player per 30-second interval
- Guess correct → artist becomes next victim
- Guess wrong → new artist takes over, same victim continues

### Timing

- Tattoo duration per round: 15-20 seconds (fast, creates urgency and chaos)
- Guess interval: Every 30 seconds (during multi-round tattooing)
- Total match: ~15-25 minutes for a full rotation

---

## Module 5: Scene & Art

### Scope

- Single fixed scene (tattoo parlor or similar)
- Static lighting (fully baked, no Lumen needed)
- One environment to build and polish

### Characters

- MetaHuman base (free)
- 1 male + 1 female body archetype with morph targets
- Minimal clothing (tank top / shorts — maximize tattoo-able skin)
- Bound pose: Simple seated/restrained animation state

### UI

- Lobby: Player list, ready button, character preview
- Customization: Slider panels, color pickers, preset grid
- In-game: Needle selector, color palette, timer, guess panel
- Results: Before/after comparison, replay of key moments

---

## Tech Stack Summary

| Component | Solution | Cost |
|-----------|----------|------|
| Engine | UE 5.5 or 5.6 | Free (< $1M revenue) |
| Character base | MetaHuman | Free |
| Tattoo system | Custom C++ (Render Target) | Dev time only |
| Networking | EOS + EIK | Free |
| Matchmaking/Relay | EOS P2P | Free |
| Save sync | Steam Cloud | Free |
| Reference material | Surface Paint System (Fab) | ~$20-50 (study networking impl) |

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Soul Perspective UX not fun | HIGH | Prototype by week 4-5, iterate based on playtest |
| Tattoo stippling feel wrong | MEDIUM | Parameterize everything (frequency, spread, opacity); tune with real testers |
| MetaHuman too heavy for 6 players | LOW | LOD2-3 for gameplay; profile early; Optimized Export in 5.5+ |
| EOS setup complexity | LOW | Well-documented, official course exists, 3-4 week buffer |
| UV seam artifacts on tattoos | LOW | Pre-Skinned Local Position + UV dilation + careful UV layout on character model |

---

## Development Priority Order

1. **Week 1-2:** Project setup + ugliest possible prototype of core tattooing (flat plane, no networking, no MetaHuman — just validate the stippling Render Target loop feels right)
2. **Week 3-5:** Networking + game flow state machine (test "guess who's drawing" with placeholder art)
3. **Week 5-7:** MetaHuman integration + character customization
4. **Week 7-10:** Polish tattoo material (in-skin SSS), full needle types, persistence
5. **Week 10-12:** Scene art, UI, audio, animation
6. **Week 12-16:** Playtest, iterate soul perspective UX, bug fix, prepare EA launch

---

## File References

- [EOS Official Course](https://dev.epicgames.com/community/learning/courses/1px/unreal-engine-the-eos-online-subsystem-oss-plugin)
- [Skeletal Mesh Paint Tutorial (Blueprint)](https://forums.unrealengine.com/t/tutorial-simple-skeletal-mesh-paint-blueprint-only/255419)
- [Paint on Skeletal Mesh with Render Targets](https://dev.epicgames.com/community/learning/tutorials/2dn9/unreal-engine-paint-on-skeletal-mesh-with-render-targets)
- [Tom Looman - Rendering Wounds on Characters](https://tomlooman.com/unreal-engine-render-character-wounds/)
- [Ryan Brucks - UV Dilation](https://shaderbits.com/blog/uv-dilation)
- [Fast TArray Replication](https://ikrima.dev/ue4guide/networking/network-replication/fast-tarray-replication/)
- [Advanced Spray System (GitHub, multiplayer reference)](https://github.com/prathameshkhanzode/Advanced-Spray-System-UE5-Cpp)
- [MetaHuman Performance Settings](https://dev.epicgames.com/documentation/metahuman/performance-and-scalability-settings-for-metahumans)
- [Mutable Sample Project](https://www.fab.com/listings/209e82f6-ad40-4253-b565-d2f65b12efe7)
- [Surface Paint System (Fab, networking reference)](https://www.fab.com/listings/d0362b96-36e1-484d-a87b-f864ab0be306)
- [UnrealPainter (GitHub, MIT)](https://github.com/DimaChaichan/UnrealPainter)
