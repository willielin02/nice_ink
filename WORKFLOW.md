# Nice Ink - Development Workflow (Revised)

## Tooling Reality

With VibeUE + UnrealClaude MCP connected:

| Capability | Tool | Status |
|------------|------|--------|
| Execute Python in Editor | `execute_python_code` | ✅ |
| Capture viewport screenshot | `unreal_capture_viewport` | ✅ |
| Start/Stop PIE | `editor_control` | ✅ |
| Spawn/move/delete actors | `unreal_spawn_actor` etc. | ✅ |
| Set any actor property | `unreal_set_property` | ✅ |
| Search/import/save assets | `manage_asset` | ✅ |
| Read editor logs | `read_logs` | ✅ |
| Write C++ source files | File system access | ✅ |
| Compile C++ | Command line build | ✅ |
| Visual verification | Viewport capture | ✅ |

**Result: I can independently build, verify, and iterate without human visual feedback.**

---

## What Genuinely Requires The User

| Item | Why I Can't Do It | When |
|------|------------------|------|
| EOS Developer Portal credentials | Web form + human verification | Before online multiplayer testing |
| Steamworks app setup | Web form + account | Before Steam integration |
| Recruit 3-5 playtesters | Social activity | Before EA launch |
| Final "is this fun?" verdict | Subjective human judgment | After all systems complete |

**Everything else — code, materials, assets, scenes, UI, testing — I do independently.**

---

## Execution Plan

### Phase 1: Core Tattoo Prototype (Self-contained)

**Goal:** A flat plane in the scene that I can paint dots on via PIE, and visually verify through viewport capture.

**Steps:**
1. Create C++ module (NiceInk) with Build.cs and Target files
2. Compile via command line
3. Via Python: create a Render Target (1024x1024, RGBA8, Clamped)
4. Via Python: create a Material that samples the RT and applies to a plane
5. Via Python: create a Dynamic Material Instance at runtime
6. Write C++ `UTattooComponent` that does stippling to the RT on mouse input
7. Spawn a StaticMeshActor (plane) with the tattoo material
8. Start PIE → capture viewport → verify dots appear
9. Iterate until correct

**Known pitfalls & pre-planned solutions:**
- DrawMaterialToRenderTarget resets RT each call → use BeginDrawCanvasToRenderTarget/End batch
- RT needs Clamped addressing → set via Python or C++ at creation
- RT format RGBA16f wastes memory → use RTF_RGBA8

**Verification:** Viewport capture after PIE shows dots on the plane.

---

### Phase 2: Needle Simulation

**Goal:** Multiple needle types with realistic stippling behavior.

**Steps:**
1. Write C++ `UTattooNeedle` with FNeedleConfig (type, frequency, dot pattern, spread)
2. Write C++ `UTattooPalette` with color array
3. Implement accumulator pattern in Tick (handles 100+ Hz dot rate at 60fps)
4. Implement inter-dot interpolation (fills gaps when moving fast, still as discrete dots)
5. Implement additive blend with saturation clamp (prevent over-bright)
6. Test each needle type via PIE + viewport capture

**Known pitfalls:**
- 100Hz strikes at 60fps Tick → accumulator while-loop pattern
- Additive blend overflow → Min blend or clamp RT values
- Fast movement gaps → lerp fill between positions, still discrete dots not lines

**Verification:** Viewport capture shows different dot patterns per needle type.

---

### Phase 3: Skeletal Mesh Migration

**Goal:** Tattoo system working on a 3D character (mannequin first, MetaHuman later).

**Steps:**
1. Via Python: spawn a Skeletal Mesh character (UE5 mannequin, available in engine content)
2. Capture reference pose using Pre-Skinned Local Position method (one-time SceneCapture2D)
3. Create unwrap material via Python (outputs PreSkinnedLocalPosition as Emissive)
4. Migrate TattooComponent to work with skeletal mesh UV lookup
5. Use Tom Looman's approach: material compares PreSkinnedLocalPosition with HitLocation, sphere mask → draw to RT
6. Apply tattoo RT to character's skin material via Dynamic Material Instance
7. Test: PIE → click on character → viewport capture → verify dot appears on body

**Known pitfalls:**
- FindCollisionUV returns (0,0) on skeletal mesh → use Pre-Skinned Local Position method
- SceneCapture per-frame is expensive → capture once at match start, store as static texture
- Physics Asset must align with mesh → verify collision capsules match character geometry
- Face and Body are separate meshes on MetaHuman → handle independently (later phase)

**Verification:** Viewport capture shows dot on the 3D character at the clicked location.

---

### Phase 4: "In-Skin" Material

**Goal:** Tattoo looks embedded in skin, not like a sticker on top.

**Steps:**
1. Create Material Function MF_TattooBlend via Python:
   - Lerp tattoo color into BaseColor at 80% opacity
   - Lerp tattoo color into SubsurfaceColor at 50% × 30% intensity
   - Keep Normal Map (skin pores) unchanged above tattoo
   - Reduce Roughness slightly in tattoo areas
2. If using MetaHuman: fork M_Head_Baked → M_Head_NiceInk, insert MF_TattooBlend
3. If using mannequin: create simple SSS material with MF_TattooBlend
4. Test: viewport capture comparing "before" and "after" tattoo application

**Known pitfalls:**
- MetaHuman material hierarchy is complex → fork the baked material, don't modify originals
- MetaHuman updates may overwrite originals → our fork is safe under /Game/ not /MetaHumans/
- SSS not visible without proper lighting → ensure directional light exists in scene

**Verification:** Viewport capture shows tattoo with visible skin texture (pores) on top.

---

### Phase 5: Character Customization

**Goal:** Player can adjust face/body via morph target sliders.

**Steps:**
1. Via Python: query all available morph targets on the character mesh (GetAllMorphTargetNames)
2. Write C++ `UCharacterCustomizer` component:
   - Maps display names ("Jaw Width") to combinations of FACS morph targets
   - Exposes SetFaceMorph(FName, float) and SetBodyType(EBodyType) to Blueprint
   - Serializes all settings into FCharacterAppearance struct
3. Via Python: test morph targets by setting values and capturing viewport
4. Verify visual changes through viewport capture at different morph values

**Known pitfalls:**
- MetaHuman FACS names are not intuitive → C++ mapping layer translates to user-friendly names
- Morph targets may not exist on non-MetaHuman mannequin → test with GetAllMorphTargetNames first
- LOD switching may break morphs → force LOD0 during customization

**If using mannequin (no morph targets available):** Skip this phase until MetaHuman is integrated. Provide placeholder UI structure.

**Verification:** Viewport capture shows character face/body changing with different morph values.

---

### Phase 6: Game Flow State Machine

**Goal:** Complete round loop: select victim → bind → tattoo → guess → reveal → next round.

**Steps:**
1. Write C++ `ANiceInkGameMode` with ENiceInkPhase state enum
2. Write C++ `ANiceInkGameState` tracking current phase, active artist, victim
3. Write C++ `ANiceInkPlayerState` tracking per-player data (tattoo history, scores)
4. Implement timer system (15-20 sec tattoo phase, 30 sec guess intervals)
5. Create soul perspective camera: C++ camera actor that detaches from victim
6. Create basic UMG widgets via Python:
   - Needle selector panel
   - Color palette picker
   - Guess panel (select a player)
   - Timer display
   - Phase transition overlay
7. Test: PIE → verify state transitions via log output and viewport

**Known pitfalls:**
- Single player testing limits game flow validation → test with simulated bot players
- Widget creation via Python requires WidgetService → use execute_python_code
- Camera transitions need smooth interpolation → use FMath::VInterpTo

**Verification:** Logs show correct state transitions; viewport shows UI elements.

---

### Phase 7: Multiplayer Networking

**Goal:** Two players can connect, and tattoo strokes sync between them.

**Steps:**
1. Write C++ networking layer:
   - FTattooStroke USTRUCT with NetSerialize
   - ServerRPC_ApplyStrokes (Unreliable)
   - MulticastRPC_ApplyStrokes (Unreliable)
   - FCharacterAppearance replication via RepNotify
   - Stroke history using Fast TArray Replication for late-join
2. For LOCAL testing (no EOS needed):
   - Use PIE with 2 players (Net Mode: Play As Listen Server + 1 Client)
   - All replication logic works in PIE without EOS
   - This tests the complete networking code path
3. EOS integration (WHEN user provides credentials):
   - Add EOS subsystem configuration to DefaultEngine.ini
   - Lobby creation / join flow
   - P2P relay connection

**Known pitfalls:**
- PIE shared RT issue → each player instance must own separate RT UObjects
- Unreliable RPC may drop packets → acceptable for cosmetic dots
- RPC 64KB limit → batch only 10-20 strokes per call (~400 bytes)
- Late-join replay of full history → Fast TArray Replication handles efficiently

**Verification:** PIE with 2 windows: draw on window 1, see dots appear on window 2.

---

### Phase 8: Persistence + Scene

**Goal:** Tattoos save permanently; game has a proper room/scene.

**Steps:**
1. Write C++ save/load:
   - FImageUtils::GetRenderTargetImage() → compress PNG → FFileHelper::SaveArrayToFile()
   - Load: FFileHelper::LoadFileToArray() → FImageUtils::ImportBufferAsTexture2D() → draw to RT
2. Create game scene via Python + spawn actors:
   - Simple room: floor, 4 walls, ceiling (box brushes or static meshes)
   - Chair/bench for victim
   - Lighting: a few point lights + directional light
   - Or: download a free room asset from Fab via manage_asset(action='import')
3. Save scene as a persistent level

**Known pitfalls:**
- ExportRenderTarget fails in packaged builds → use FImageUtils instead (already planned)
- Steam Cloud setup needs Steamworks account → defer to user
- PNG file size ~200-500KB per character → negligible

**Verification:** PIE → tattoo → close PIE → reopen PIE → tattoo persists on character.

---

### Phase 9: MetaHuman Integration

**Goal:** Replace mannequin with MetaHuman for realistic character.

**Steps:**
1. Via Python: check if MetaHuman plugin is available and functional
2. Download/create a MetaHuman preset using MetaHuman Creator (built into UE 5.7)
3. Fork MetaHuman skin material → add TattooBlend layer
4. Attach TattooComponent to MetaHuman character
5. Adjust Physics Asset for accurate tattoo placement
6. Wire up morph targets for character customization
7. Test face + body painting via PIE + viewport capture

**Known pitfalls:**
- MetaHuman face and body are separate skeletal meshes → each needs own TattooComponent + RT
- MetaHuman LOD affects morph targets → force LOD0 during customization, LOD2-3 during gameplay
- MetaHuman Optimized Export (60MB vs 800MB) → use optimized pipeline
- Vertex color painting destroys morph targets in UE 5.6+ → only use RT approach (already planned)

**Verification:** Viewport capture shows tattoo on MetaHuman face with skin pores visible on top.

---

### Phase 10: Polish + EA Prep

**Goal:** Game is shippable to Early Access.

**Steps:**
1. Audio hookup points (SFX for needle buzz, dot impact, UI clicks)
2. Visual polish (particle effects for ink splash, camera shake on hit)
3. UI polish (main menu, lobby screen, results screen)
4. Build + package test via RunUAT
5. Final multiplayer test (requires EOS credentials from user)

**Deferred to user:**
- EOS Developer Portal account + credentials
- Steamworks app registration
- Recruit playtesters for "is this fun?" validation
- Audio asset creation/sourcing
- Store page assets (screenshots, trailer)

---

## What I Do While User Is Away

**I execute Phases 1-6 and 8 independently**, in order. Phase 7 local testing (PIE 2-player) also works without EOS. Phase 9 depends on MetaHuman availability in the project.

At each phase completion, I verify via viewport capture and logs. If something breaks, I debug using logs + viewport + Python inspection.

**Estimated wall-clock time:** Dominated by C++ compile waits (~2-5 min per iteration) and Editor restart times. Pure coding is fast.

---

## Anti-Pattern List (unchanged)

| Don't | Why | Do Instead |
|-------|-----|------------|
| Use DoN's plugin | Stopped at UE 5.2 | Self-build RT system |
| Modify MetaHuman original materials | Updates overwrite | Fork and rename |
| Use FindCollisionUV on Skeletal Mesh | Returns 0,0 always | Pre-Skinned Local Position |
| SceneCapture every frame | 1.6-4.5ms cost | Capture once at match start |
| Sync texture data over network | Bandwidth explosion | Sync stroke commands only |
| Use Vertex Color painting | Low resolution, breaks morph targets | Render Target texture painting |
| Start with MetaHuman | Too many moving parts | Start with mannequin, swap later |
| Create morph targets in Blender | Unnecessary | MetaHuman has 130+ built-in |
