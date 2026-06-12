#include "headers/CruxSword.h"
#include "../movement/CruxCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Engine/World.h"

// ============================================================================
//  CONSTRUCTION
// ============================================================================

ACruxSword::ACruxSword()
{
    PrimaryActorTick.bCanEverTick = true; // Needed for PrimarySlashCDTimer countdown

    // Identity
    WeaponSlot        = EWeaponSlot::Slot2_Bridger;
    WeaponDisplayName = FText::FromString(TEXT("Sword"));

    // Damage
    PrimaryDamage   = 25.f;
    SecondaryDamage = 15.f; // The lunge's value is speed, not burst damage
    FlowHookDamage  = 35.f;

    // Melee trace — generous sphere for reliable, forgiving slashes
    MeleeTraceLength = 100.f;
    MeleeTraceRadius = 14.f;
}

void ACruxSword::BeginPlay()
{
    Super::BeginPlay();
}

void ACruxSword::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (PrimarySlashCDTimer > 0.f)
        PrimarySlashCDTimer = FMath::Max(0.f, PrimarySlashCDTimer - DeltaTime);
}

// ============================================================================
//  PRIMARY FIRE — Fast horizontal slash
// ============================================================================
//
//  Design intent: accessible, fast, low commitment. A new player spamming LMB
//  should deal relevant damage and feel responsive. The tiny forward push keeps
//  them moving without disrupting momentum — the Sword never fights the player.
//
// ============================================================================

void ACruxSword::PrimaryFire()
{
    Super::PrimaryFire(); // Guard: bIsEquipped, owner valid

    if (PrimarySlashCDTimer > 0.f) return;
    PrimarySlashCDTimer = PrimarySlashCooldown;

    FHitResult Hit;
    if (MeleeSweep(Hit))
    {
        ApplyWeaponDamage(Hit.GetActor(), PrimaryDamage, Hit);

        // Tiny additive forward push — nudges momentum without overwriting it.
        // Keeps the "movement is consistent" feel that makes the Sword beginner-friendly.
        LaunchOwner(GetOwnerForward() * 80.f, /*bAdditive=*/true);

        OnFlowHook(Hit, ClassifyHit(Hit));
    }
}

// ============================================================================
//  SECONDARY FIRE — Launch-slash (Micro-Charge)
// ============================================================================
//
//  TAP:  Fast gap-close. Additive — preserves existing momentum. Air-steering
//        still works. Good for chasing targets mid-combo.
//  HOLD: Full commitment. Trajectory locked for LungeLockDuration. Gravity
//        suppressed so the character flies level. Deals damage to the first
//        enemy in the trajectory at the moment of SecondaryFire.
//
//  Impulse magnitudes come from the CMC's UPROPERTY values so they can be
//  tweaked in one place and flow through to all weapons.
//
// ============================================================================

void ACruxSword::SecondaryFire(bool bHeld)
{
    Super::SecondaryFire(bHeld); // Guard

    // CMC handles the cooldown gate and applies its own LaunchCharacter impulse.
    // We then overwrite velocity directly for precise shaping below.
    CruxMovement->FireMicroCharge(bHeld);

    const float   ImpulseStrength = bHeld
        ? CruxMovement->MicroChargeHeldImpulse
        : CruxMovement->MicroChargeTapImpulse;
    const FVector LungeDir    = GetOwnerForward();
    const FVector LungeImpulse = LungeDir * ImpulseStrength;

    // Replace XY with lunge velocity. Keep Z for tap (gravity handles arc),
    // suppress Z for held (player flies perfectly level).
    FVector NewVelocity = GetOwnerVelocity();
    NewVelocity.X = LungeImpulse.X;
    NewVelocity.Y = LungeImpulse.Y;
    if (bHeld) NewVelocity.Z = 0.f;

    CruxMovement->Velocity = NewVelocity;

    // ── Held: lock trajectory ──────────────────────────────────────────────────
    if (bHeld && !bLungeLockActive)
    {
        bLungeLockActive = true;

        CruxMovement->GravityScale = LungeGravityScale; // Near-zero — fly level
        CruxMovement->AirControl   = 0.f;               // No steering

        GetWorldTimerManager().SetTimer(
            LungeLockTimerHandle,
            this, &ACruxSword::EndLungeLock,
            LungeLockDuration,
            /*bLoop=*/false);
    }

    // ── Hit check along lunge path ─────────────────────────────────────────────
    // The slash deals damage to the first enemy in the trajectory immediately.
    // A slightly longer trace is used for the lunge to match the distance covered.
    FHitResult LungeHit;
    if (MeleeSweep(LungeHit, MeleeTraceLength * 1.5f))
    {
        ApplyWeaponDamage(LungeHit.GetActor(), SecondaryDamage, LungeHit);
        OnFlowHook(LungeHit, ClassifyHit(LungeHit));
    }
}

// ============================================================================
//  FLOW HOOK — Zero-gravity window on any successful hit
// ============================================================================
//
//  Every hit (enemy OR surface) is a movement opportunity. Gravity is zeroed
//  for SwordFlowHookDuration seconds — during this window the player floats
//  and can:
//    - Chain another micro-charge (cooldown is also reset here)
//    - Run into a wall to start a wall-run without losing height
//    - Jump cleanly from the float without fighting gravity
//
//  Consecutive hits re-start the window rather than stacking — prevents
//  indefinite float while still rewarding rapid hit chains.
//
// ============================================================================

void ACruxSword::OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target)
{
    Super::OnFlowHook(HitResult, Target); // Guard

    if (Target == EFlowHookTarget::Miss) return;

    // Reset any in-progress window so consecutive hits feel seamless
    if (bFlowHookWindowActive)
        GetWorldTimerManager().ClearTimer(FlowHookTimerHandle);

    bFlowHookWindowActive = true;

    // Zero gravity — the CMC reads GravityScale every tick
    CruxMovement->GravityScale = 0.f;

    // Reset micro-charge cooldown — enables the immediate chained dash
    CruxMovement->MicroChargeCDTimer = 0.f;

    GetWorldTimerManager().SetTimer(
        FlowHookTimerHandle,
        this, &ACruxSword::EndFlowHookWindow,
        SwordFlowHookDuration,
        /*bLoop=*/false);
}

// ============================================================================
//  TIMER CALLBACKS
// ============================================================================

void ACruxSword::EndLungeLock()
{
    bLungeLockActive = false;
    // Restore CMC defaults. These values match the CMC constructor.
    CruxMovement->GravityScale = 1.4f;
    CruxMovement->AirControl   = 0.6f;
}

void ACruxSword::EndFlowHookWindow()
{
    bFlowHookWindowActive = false;

    // Restore gravity only if the lunge lock isn't also suppressing it.
    // Whichever timer fires last wins — no double-restoration needed.
    if (!bLungeLockActive)
        CruxMovement->GravityScale = 1.4f;
}
