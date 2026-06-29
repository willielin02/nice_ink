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

MetaHuman base + runtime morph targets. Goal: player can make character resemble themselves in ~3 minutes.

### Core Class

```
UCharacterCustomizer     — ActorComponent on player character.
FCharacterAppearance     — Serializable struct holding all customization params.
```

### Customization Parameters

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

### UX Flow

1. Select one of 8-10 ethnic/gender presets as starting point
2. Adjust face sliders (grouped: eyes, nose, mouth, jaw)
3. Pick skin tone from a gradient picker
4. Choose hair
5. Select body type (3-4 presets with optional slider fine-tune)
6. Confirm → serialize to FCharacterAppearance → replicate to all players

### Technical Details

- MetaHuman LOD: Use LOD2-3 during gameplay (4-6 players), LOD0-1 for tattoo close-ups
- MetaHuman Optimized Export (UE 5.5+): 60MB per character vs 800MB
- Morph targets driven via `USkeletalMeshComponent::SetMorphTarget(FName, float)`
- All in C++; Blueprint gets a simple "Apply Preset" and individual slider nodes

### Exposed Blueprint Ports

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
