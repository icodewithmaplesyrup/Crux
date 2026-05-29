#include "CruxSword.h"
#include "CruxCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Engine/World.h"

// ============================================================================
//  CONSTRUCTION
// ============================================================================

ACruxSword::ACruxSword()
{
    PrimaryActorTick.bCanEverTick = true; // Needed for slash cooldown countdown
    WeaponSlot    = EWeaponSlot::Slot2_Bridger;
    PrimaryDamage = 25.f;
    SecondaryDamage = 15.f; // The lunge's value is speed, not damage
    FlowHookDamage  = 35.f;
    MeleeTraceLength = 100.f;
    MeleeTraceRadius = 14.f; // Generous width — slash should feel reliable
    WeaponDisplayName = FText::FromString(TEXT("Sword"));
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
//  Design intent: accessible, fast, low commitment. A new player spamming Left
//  Click should deal relevant damage and feel responsive. The tiny forward push
//  keeps them moving without disrupting momentum.
//
// ============================================================================

void ACruxSword::PrimaryFire()
{
    Super::PrimaryFire(); // Guard checks

    if (PrimarySlashCDTimer > 0.f) return; // Respect slash rate

    PrimarySlashCDTimer = PrimarySlashCooldown;

    FHitResult Hit;
    if (MeleeSweep(Hit))
    {
        // Deal damage
        ApplyWeaponDamage(Hit.GetActor(), PrimaryDamage, Hit);

        // Tiny forward push — does NOT zero velocity, just nudges.
        // Keeps the "movement stays consistent" feel of the Sword.
        const FVector MicroPush = GetOwnerForward() * 80.f;
        LaunchOwner(MicroPush, /*bAdditive=*/true);

        // Trigger Flow Hook
        const EFlowHookTarget Target = ClassifyHit(Hit);
        OnFlowHook(Hit, Target);
    }
}

// ============================================================================
//  SECONDARY FIRE — Launch-slash (Micro-Charge)
// ============================================================================
//
//  This is the weapon-shaped version of the CMC's FireMicroCharge().
//  The CMC handles cooldown tracking; we shape the impulse and lock trajectory.
//
//  TAP:  600-700 UU/s forward. Quick gap-close. Air-steering still works.
//  HOLD: 1200-1400 UU/s forward. Full commitment. Trajectory locked for
//        LungeLockDuration seconds — player flies straight, no steering.
//
//  The lunge's gravity scale is nearly zero so the character doesn't arc
//  downward during the lunge window. Feels like a horizontal rocket.
//
// ============================================================================

void ACruxSword::SecondaryFire(bool bHeld)
{
    Super::SecondaryFire(bHeld);

    // Ask the CMC to fire its impulse. This handles cooldown checks and
    // the tap/held impulse magnitudes defined in the CMC's UPROPERTY values.
    CruxMovement->FireMicroCharge(bHeld);

    // ── Shape the lunge ──────────────────────────────────────────────────────
    const float ImpulseStrength = bHeld
        ? CruxMovement->MicroChargeHeldImpulse
        : CruxMovement->MicroChargeTapImpulse;

    const FVector LungeDir    = GetOwnerForward();
    const FVector LungeImpulse = LungeDir * ImpulseStrength;

    // Redirect velocity entirely into the forward lunge.
    // We replace XY and keep Z so jumps during a tap feel natural.
    FVector NewVelocity        = GetOwnerVelocity();
    NewVelocity.X              = LungeImpulse.X;
    NewVelocity.Y              = LungeImpulse.Y;
    // Z: keep existing Z for tap (gravity handles it), suppress for held lunge
    if (bHeld) NewVelocity.Z   = 0.f;

    CruxMovement->Velocity = NewVelocity;

    // ── Held: lock trajectory ─────────────────────────────────────────────────
    if (bHeld && !bLungeLockActive)
    {
        bLungeLockActive = true;

        // Suppress gravity during the lunge so the character flies level
        CruxMovement->GravityScale = LungeGravityScale;

        // Lock air control to zero — player cannot steer during committed lunge
        CruxMovement->AirControl = 0.f;

        GetWorldTimerManager().SetTimer(
            LungeLockTimerHandle,
            this, &ACruxSword::EndLungeLock,
            LungeLockDuration,
            /*bLoop=*/false);
    }

    // ── Melee check during lunge ──────────────────────────────────────────────
    // Immediately check for a hit along the lunge path. This lets the slash
    // deal damage to the first enemy in the trajectory.
    FHitResult LungeHit;
    if (MeleeSweep(LungeHit, MeleeTraceLength * 1.5f)) // Slightly longer for lunge
    {
        ApplyWeaponDamage(LungeHit.GetActor(), SecondaryDamage, LungeHit);
        OnFlowHook(LungeHit, ClassifyHit(LungeHit));
    }
}

// ============================================================================
//  FLOW HOOK — Zero-gravity window on any successful hit
// ============================================================================
//
//  The Sword's identity: every hit is a movement opportunity.
//
//  On a successful hit (enemy OR surface), gravity is zeroed for
//  SwordFlowHookDuration seconds. During this window the player floats
//  in place — they are free to:
//    - Fire another micro-charge (CMC cooldown is also reset here)
//    - Turn and run into a wall to start a wall-run
//    - Jump cleanly without losing height to gravity
//
//  This creates the "gravity reset" that the GDD describes as the Sword's
//  Flow Hook: "resets gravity for a split second, allowing the dash to
//  chain directly into a wall-run or another weapon's movement without
//  losing forward speed."
//
// ============================================================================

void ACruxSword::OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target)
{
    Super::OnFlowHook(HitResult, Target);

    if (Target == EFlowHookTarget::Miss) return;

    // Cancel any existing window before starting a new one (consecutive hits
    // should extend the float, not stack forever)
    if (bFlowHookWindowActive)
        GetWorldTimerManager().ClearTimer(FlowHookTimerHandle);

    bFlowHookWindowActive = true;

    // ── Zero gravity ──────────────────────────────────────────────────────────
    // Store the real gravity scale so we can restore it when the window ends.
    // We write directly to GravityScale — the CMC reads this every tick to
    // compute the gravitational force applied to Velocity.Z.
    CruxMovement->GravityScale = 0.f;

    // ── Reset micro-charge cooldown ───────────────────────────────────────────
    // This is what makes the chained dash possible. The player can immediately
    // fire another micro-charge after the hit without waiting for cooldown.
    CruxMovement->MicroChargeCDTimer = 0.f;

    // ── Schedule gravity restoration ──────────────────────────────────────────
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

    // Restore standard gravity and air control
    // Use the CMC's own default values rather than hardcoding
    CruxMovement->GravityScale = 1.4f; // Matches CMC constructor
    CruxMovement->AirControl   = 0.6f; // Matches CMC constructor
}

void ACruxSword::EndFlowHookWindow()
{
    bFlowHookWindowActive = false;

    // Restore gravity. If a lunge lock was ALSO active, leave it — EndLungeLock
    // will restore gravity when the lunge expires. Whichever timer fires last wins.
    if (!bLungeLockActive)
    {
        CruxMovement->GravityScale = 1.4f;
    }
}
