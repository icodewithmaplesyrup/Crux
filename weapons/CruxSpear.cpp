#include "CruxSpear.h"
#include "CruxCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Engine/World.h"

// ============================================================================
//  CONSTRUCTION
// ============================================================================

ACruxSpear::ACruxSpear()
{
    PrimaryActorTick.bCanEverTick = true;
    WeaponSlot       = EWeaponSlot::Slot2_Bridger;
    PrimaryDamage    = 22.f;
    SecondaryDamage  = 10.f; // Lunge is about distance, not burst damage
    FlowHookDamage   = 0.f;  // Wall-kick is a movement event, not a damage event
    MeleeTraceLength = 160.f; // Longer reach than Sword
    MeleeTraceRadius = 6.f;   // Narrow line — precision required
    WeaponDisplayName = FText::FromString(TEXT("Spear"));
}

void ACruxSpear::BeginPlay()
{
    Super::BeginPlay();
}

void ACruxSpear::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (ThrustCDTimer > 0.f)
        ThrustCDTimer = FMath::Max(0.f, ThrustCDTimer - DeltaTime);
}

// ============================================================================
//  PRIMARY FIRE — Rapid straight-forward thrust
// ============================================================================
//
//  Narrow hitbox, long reach. No movement manipulation. The Spear's primary
//  is a pure damage tool — sustained poke from outside melee range.
//  The line trace means the player must aim more carefully than the Sword.
//
// ============================================================================

void ACruxSpear::PrimaryFire()
{
    Super::PrimaryFire();
    if (ThrustCDTimer > 0.f) return;

    ThrustCDTimer = ThrustCooldown;

    FHitResult Hit;
    if (MeleeSweep(Hit)) // Uses the narrow MeleeTraceRadius = 6
    {
        ApplyWeaponDamage(Hit.GetActor(), PrimaryDamage, Hit);

        // Classify and trigger Flow Hook.
        // For primary fire: enemy hits deal damage (above), surface hits do nothing
        // on primary — the wall-kick is reserved for the lunge's miss case.
        const EFlowHookTarget Target = ClassifyHit(Hit);
        OnFlowHook(Hit, Target);
    }
}

// ============================================================================
//  SECONDARY FIRE — XL lunge (Micro-Charge, fully committed)
// ============================================================================
//
//  The lunge is the Spear's signature. Unlike the Sword's lunge which deals
//  damage mid-flight, the Spear lunge is pure movement — you commit to a
//  direction, travel the full distance, and either arrive at your target or
//  sail past them.
//
//  At the END of the lunge (or whenever the trace finds a surface), the wall-
//  kick Flow Hook fires. If you aimed at an enemy and hit them — great, damage.
//  If you aimed at a wall — the wall-kick redirects you at high speed.
//  If you aimed at nothing — you land with all that speed intact.
//
// ============================================================================

void ACruxSpear::SecondaryFire(bool bHeld)
{
    Super::SecondaryFire(bHeld);

    // CMC fires the base impulse and handles cooldown
    CruxMovement->FireMicroCharge(bHeld);

    const float ImpulseStrength = bHeld
        ? CruxMovement->MicroChargeHeldImpulse * 1.2f // XL — slightly larger than Sword
        : CruxMovement->MicroChargeTapImpulse;

    const FVector LungeDir = GetOwnerForward();

    // ── Replace velocity entirely — committed linear momentum ──────────────────
    // "Entirely committed linear momentum — you fly straight."
    // Z is zeroed on held lunge so there's no arc. On tap, preserve Z for
    // natural gravity interaction mid-air.
    FVector NewVelocity    = FVector::ZeroVector;
    NewVelocity           += LungeDir * ImpulseStrength;
    if (!bHeld) NewVelocity.Z = GetOwnerVelocity().Z;

    CruxMovement->Velocity = NewVelocity;

    // ── Lock trajectory for held lunge ────────────────────────────────────────
    if (bHeld && !bLungeLockActive)
    {
        bLungeLockActive = true;
        CruxMovement->GravityScale = 0.05f; // Nearly no drop during rail lunge
        CruxMovement->AirControl   = 0.f;

        GetWorldTimerManager().SetTimer(
            LungeLockTimerHandle,
            this, &ACruxSpear::EndLungeLock,
            LungeLockDuration,
            false);
    }

    // ── Trace along lunge path for hit resolution ─────────────────────────────
    // Use a longer trace matching the lunge distance.
    // Check for BOTH pawn and world static hits — we need to know if we
    // hit a surface (wall-kick) or an enemy.
    FHitResult LungeHit;
    const FVector EyeLoc   = OwnerCharacter->GetPawnViewLocation();
    const FVector TraceEnd = EyeLoc + LungeDir * LungeTraceLength;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(OwnerCharacter);
    Params.AddIgnoredActor(this);

    // Multi-channel sweep: catches pawns AND world geometry in one pass
    bool bHitSomething = GetWorld()->SweepSingleByObjectType(
        LungeHit,
        EyeLoc,
        TraceEnd,
        FQuat::Identity,
        FCollisionObjectQueryParams(ECC_TO_BITFIELD(ECC_Pawn) | ECC_TO_BITFIELD(ECC_WorldStatic)),
        FCollisionShape::MakeSphere(8.f),
        Params);

    if (bHitSomething)
    {
        const EFlowHookTarget Target = ClassifyHit(LungeHit);
        if (Target == EFlowHookTarget::Enemy)
            ApplyWeaponDamage(LungeHit.GetActor(), SecondaryDamage, LungeHit);

        OnFlowHook(LungeHit, Target);
    }
}

// ============================================================================
//  FLOW HOOK — Wall-kick on surface hit
// ============================================================================
//
//  This is the Spear's signature movement payoff.
//
//  On a SURFACE hit, we perform a physics reflection:
//    1. Get the incoming velocity (the lunge velocity)
//    2. Reflect it off the wall's normal vector
//    3. Scale up by WallKickMultiplier
//    4. Enforce a minimum speed floor (WallKickMinSpeed)
//    5. Apply via LaunchCharacter with override = true (replaces velocity)
//
//  The reflected vector naturally carries the "sharp angle" feel — if you
//  run perpendicular into a wall, you bounce straight back. If you hit at 45°,
//  you bounce at 45° to the other side. Physics handles the geometry.
//
//  On an ENEMY hit during primary fire: no special movement. On an ENEMY hit
//  during a lunge, no wall-kick either — the value was reaching the target.
//
// ============================================================================

void ACruxSpear::OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target)
{
    Super::OnFlowHook(HitResult, Target);

    // Wall-kick only fires on surface hits
    if (Target != EFlowHookTarget::Surface) return;

    const FVector WallNormal    = HitResult.Normal;
    const FVector IncomingVel   = GetOwnerVelocity();

    // ── Reflect velocity off the wall normal ──────────────────────────────────
    //
    //  FVector::MirrorByVector reflects IncomingVel across WallNormal.
    //  This is: V' = V - 2(V·N)N — standard specular reflection formula.
    //  Result: the player bounces off the wall at the mirror angle of approach.
    //
    const FVector ReflectedVel  = IncomingVel.MirrorByVector(WallNormal);

    // ── Scale the kick ────────────────────────────────────────────────────────
    FVector KickVelocity = ReflectedVel * WallKickMultiplier;

    // ── Enforce minimum speed ─────────────────────────────────────────────────
    // If the incoming velocity was small (player nearly stopped at the wall),
    // the reflection would be tiny. Enforce a floor so the kick is always useful.
    const float CurrentKickSpeed = KickVelocity.SizeSquared();
    const float MinSpeedSq       = FMath::Square(WallKickMinSpeed);
    if (CurrentKickSpeed < MinSpeedSq)
    {
        KickVelocity = KickVelocity.GetSafeNormal() * WallKickMinSpeed;
    }

    // ── Apply the wall-kick ───────────────────────────────────────────────────
    // LaunchOwner with bAdditive=false replaces velocity entirely.
    // This is intentional — the wall-kick IS the new trajectory.
    LaunchOwner(KickVelocity, /*bAdditive=*/false);

    // End any active lunge lock — the wall-kick unlocks steering
    if (bLungeLockActive)
    {
        bLungeLockActive = false;
        GetWorldTimerManager().ClearTimer(LungeLockTimerHandle);
        EndLungeLock();
    }
}

// ============================================================================
//  TIMER CALLBACKS
// ============================================================================

void ACruxSpear::EndLungeLock()
{
    bLungeLockActive = false;
    CruxMovement->GravityScale = 1.4f;
    CruxMovement->AirControl   = 0.6f;
}
