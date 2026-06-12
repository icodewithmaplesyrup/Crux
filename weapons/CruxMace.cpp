#include "headers/CruxMace.h"
#include "../movement/CruxCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"

// ============================================================================
//  CONSTRUCTION
// ============================================================================

ACruxMace::ACruxMace()
{
    PrimaryActorTick.bCanEverTick = true;
    WeaponSlot       = EWeaponSlot::Slot1_Launcher;
    PrimaryDamage    = 60.f;  // Highest single-hit damage in the roster
    SecondaryDamage  = 0.f;   // Rocket jump deals no external damage
    FlowHookDamage   = 999.f; // Sky Crusher — handled separately with SkyCrusherDamage
    MeleeTraceLength = 80.f;  // Short but wide
    MeleeTraceRadius = 28.f;  // Wide sphere — overhead slam should feel unavoidable
    WeaponDisplayName = FText::FromString(TEXT("Mace"));
}

void ACruxMace::BeginPlay()
{
    Super::BeginPlay();
}

void ACruxMace::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (SlamCDTimer > 0.f)
        SlamCDTimer = FMath::Max(0.f, SlamCDTimer - DeltaTime);

    // ── Dive-bomb landing detection ───────────────────────────────────────────
    // While a dive-bomb is active we check each tick for ground contact.
    // When we land, spawn a large shockwave and clear the dive state.
    if (bDiveBombActive && !CruxMovement->IsFalling())
    {
        // We just landed from a dive-bomb — spawn the amplified shockwave
        const FVector LandingPos = OwnerCharacter->GetActorLocation();
        SpawnShockwave(LandingPos, SlamShockwaveRadius * 2.f, SlamShockwaveForce * 2.f);

        bDiveBombActive = false;

        // Check for a Sky Crusher kill — did we land on top of a Pawn?
        // Look slightly below and ahead to catch enemies we landed on top of.
        FHitResult StompHit;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(OwnerCharacter);

        const FVector SweepStart = LandingPos;
        const FVector SweepEnd   = LandingPos - FVector(0, 0, 80.f); // Check just below

        bool bHitPawn = GetWorld()->SweepSingleByChannel(
            StompHit,
            SweepStart,
            SweepEnd,
            FQuat::Identity,
            ECC_Pawn,
            FCollisionShape::MakeSphere(60.f), // Wide — landing "on top of" is approximate
            Params);

        if (bHitPawn && StompHit.GetActor())
        {
            OnFlowHook(StompHit, EFlowHookTarget::Enemy);
        }
    }
}

// ============================================================================
//  PRIMARY FIRE — Overhead slam with shockwave
// ============================================================================
//
//  The slam is slow but devastating. The shockwave is the area-denial payoff —
//  loose physics debris at 1:72 scale becomes a spray of chaotic hazards that
//  can hit other players. On a scanned city map, every loose pebble, soda tab,
//  and paperclip in range becomes a projectile.
//
// ============================================================================

void ACruxMace::PrimaryFire()
{
    Super::PrimaryFire();
    if (SlamCDTimer > 0.f) return;

    SlamCDTimer = SlamCooldown;

    FHitResult Hit;
    if (MeleeSweep(Hit))
    {
        ApplyWeaponDamage(Hit.GetActor(), PrimaryDamage, Hit);

        // Shockwave at the hit point, not the character's feet
        SpawnShockwave(Hit.ImpactPoint, SlamShockwaveRadius, SlamShockwaveForce);

        OnFlowHook(Hit, ClassifyHit(Hit));
    }
    else
    {
        // Missed — shockwave at feet (ground slam still creates debris scatter)
        SpawnShockwave(OwnerCharacter->GetActorLocation(), SlamShockwaveRadius * 0.6f,
            SlamShockwaveForce * 0.4f);
    }
}

// ============================================================================
//  SECONDARY FIRE — Rocket Jump (grounded) / Dive-Bomb Stomp (airborne)
// ============================================================================
//
//  This is the Mace's most complex input — the same button does fundamentally
//  different things depending on whether the player is grounded or airborne.
//  The check is simply IsFalling() from the CMC.
//
//  GROUNDED — Rocket Jump:
//    Kills XY velocity (player goes straight up, not at an angle).
//    Applies RocketJumpUpForce as an instant Z impulse.
//    This is "the highest vertical displacement in the game."
//
//  AIRBORNE — Dive-Bomb Stomp:
//    Kills XY velocity entirely. Player drops like a stone.
//    Applies DiveBombDownVelocity as a negative Z velocity (replaces falling).
//    Sets bDiveBombActive = true so Tick() can handle the landing.
//
// ============================================================================

void ACruxMace::SecondaryFire(bool bHeld)
{
    Super::SecondaryFire(bHeld);

    if (!IsOwnerAirborne())
    {
        // ── GROUNDED: Rocket Jump ──────────────────────────────────────────────
        // Zero horizontal velocity — straight-up launch, no angle.
        // This makes the rocket jump predictable and consistent.
        CruxMovement->Velocity = FVector(0.f, 0.f, 0.f);
        LaunchOwner(FVector(0.f, 0.f, RocketJumpUpForce), /*bAdditive=*/false);

        // Optional self-damage (realistic rocket jump feel)
        if (RocketJumpSelfDamage > 0.f)
        {
            FHitResult SelfHit;
            ApplyWeaponDamage(OwnerCharacter, RocketJumpSelfDamage, SelfHit);
        }
    }
    else
    {
        // ── AIRBORNE: Dive-Bomb Stomp ──────────────────────────────────────────
        // Kill all horizontal momentum — "straight to the ground."
        // Replace falling velocity with a higher downward speed.
        const FVector DiveVelocity = FVector(0.f, 0.f, -DiveBombDownVelocity);
        CruxMovement->Velocity = DiveVelocity;

        // Mark dive as active — Tick() handles the landing response
        bDiveBombActive = true;

        // Suppress gravity scale during the dive — we own the downward speed now.
        // Without this, gravity additively stacks and the landing speed is
        // non-deterministic. We control the exact landing velocity.
        CruxMovement->GravityScale = 0.f;
    }
}

// ============================================================================
//  FLOW HOOK — Sky Crusher
// ============================================================================
//
//  Only fires when the dive-bomb landing hits a Pawn (detected in Tick()).
//
//  "Landing a mid-air stomp directly on top of an enemy crushes them instantly
//  and launches the player back into the sky with doubled vertical height."
//
//  The crush kill is applied as damage. The sky launch is a LaunchCharacter
//  call with SkyCrusherBounceForce = 2 × RocketJumpUpForce.
//
//  Note: primary fire hitting an enemy does NOT trigger Flow Hook — the Mace's
//  Flow Hook is exclusively the stomp-on-enemy scenario.
//
// ============================================================================

void ACruxMace::OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target)
{
    Super::OnFlowHook(HitResult, Target);

    // Flow Hook only activates on enemy hit from a dive-bomb
    // (Primary slam hitting an enemy has no movement payoff by design)
    if (Target != EFlowHookTarget::Enemy) return;

    // ── Deal crush damage ──────────────────────────────────────────────────────
    // SkyCrusherDamage defaults to 999 — effectively an insta-kill.
    // If your game has a health system that caps damage, adjust accordingly.
    ApplyWeaponDamage(HitResult.GetActor(), SkyCrusherDamage, HitResult);

    // ── Launch back into the sky ───────────────────────────────────────────────
    // Restore gravity first (was suppressed during dive), then apply the bounce.
    CruxMovement->GravityScale = 1.4f;

    // "Doubled vertical height" = 2× the rocket jump upforce.
    // Horizontal velocity stays zero (came straight down, go straight up).
    LaunchOwner(FVector(0.f, 0.f, SkyCrusherBounceForce), /*bAdditive=*/false);

    // Reset the dive flag — landing was a kill, not a regular stomp
    bDiveBombActive = false;
}

// ============================================================================
//  SHOCKWAVE
// ============================================================================
//
//  Iterates all overlapping primitive components within the shockwave radius
//  and applies a radial impulse to those that are physics-simulated.
//  At 1:72 scale, "loose physics debris" means pebbles, soda tabs, paperclips —
//  small components that are set to SimulatePhysics = true in the map.
//
// ============================================================================

void ACruxMace::SpawnShockwave(const FVector& Origin, float Radius, float Force) const
{
    if (!GetWorld()) return;

    // Gather all overlapping components in the shockwave radius
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(OwnerCharacter);
    Params.AddIgnoredActor(this);

    GetWorld()->OverlapMultiByChannel(
        Overlaps,
        Origin,
        FQuat::Identity,
        ECC_WorldDynamic, // Physics debris is WorldDynamic
        FCollisionShape::MakeSphere(Radius),
        Params);

    for (const FOverlapResult& Overlap : Overlaps)
    {
        UPrimitiveComponent* Comp = Overlap.GetComponent();
        if (Comp && Comp->IsSimulatingPhysics())
        {
            // AddRadialImpulse: physics-correct radial force centered at Origin.
            // ERadialImpulseFalloff::RIF_Constant = same force at any range within radius.
            // Use RIF_Linear if you want center-heavy falloff.
            Comp->AddRadialImpulse(Origin, Radius, Force,
                ERadialImpulseFalloff::RIF_Constant, /*bVelChange=*/true);
        }
    }
}
