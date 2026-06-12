#include "headers/CruxSpear.h"
#include "../movement/CruxCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Engine/World.h"

// ============================================================================
//  CONSTRUCTION
// ============================================================================

ACruxSpear::ACruxSpear()
{
    PrimaryActorTick.bCanEverTick = true; // Needed for ThrustCDTimer countdown

    // Identity
    WeaponSlot        = EWeaponSlot::Slot2_Bridger;
    WeaponDisplayName = FText::FromString(TEXT("Spear"));

    // Damage
    PrimaryDamage   = 22.f;
    SecondaryDamage = 10.f; // Lunge is about distance management, not burst damage
    FlowHookDamage  = 0.f;  // Wall-kick is a movement event, not a damage event

    // Melee trace — long reach, narrow sphere (precision matters)
    MeleeTraceLength = 160.f;
    MeleeTraceRadius = 6.f;
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
//  The narrow trace means the player must aim more carefully than the Sword.
//
//  Primary fire does NOT trigger the wall-kick Flow Hook. That's reserved for
//  lunge misses — so hitting geometry on a primary doesn't accidentally waste
//  the Flow Hook's repositioning value.
//
// ============================================================================

void ACruxSpear::PrimaryFire()
{
    Super::PrimaryFire(); // Guard

    if (ThrustCDTimer > 0.f) return;
    ThrustCDTimer = ThrustCooldown;

    FHitResult Hit;
    if (MeleeSweep(Hit))
    {
        ApplyWeaponDamage(Hit.GetActor(), PrimaryDamage, Hit);

        // Only enemy hits get a Flow Hook call on primary (and there's no effect
        // defined for enemy hits — this is a hook point for future design).
        // Surface hits on primary are intentionally ignored.
        const EFlowHookTarget Target = ClassifyHit(Hit);
        if (Target == EFlowHookTarget::Enemy)
            OnFlowHook(Hit, Target);
    }
}

// ============================================================================
//  SECONDARY FIRE — XL lunge (Micro-Charge, fully committed)
// ============================================================================
//
//  The Spear lunge is pure movement — commit to a direction, travel the full
//  distance. Unlike the Sword's lunge (which deals damage mid-flight), the
//  Spear lunge's damage comes from a hit-trace at the moment of firing.
//
//  Three outcomes after the lunge fires:
//    Hit enemy  → damage + no movement bonus (value was reaching the target)
//    Hit surface → wall-kick Flow Hook (miss turned into reposition)
//    Hit nothing → full speed intact, you land wherever your lunge took you
//
// ============================================================================

void ACruxSpear::SecondaryFire(bool bHeld)
{
    Super::SecondaryFire(bHeld); // Guard

    CruxMovement->FireMicroCharge(bHeld);

    // Spear's XL impulse is 1.2× the CMC's base held magnitude
    const float ImpulseStrength = bHeld
        ? CruxMovement->MicroChargeHeldImpulse * 1.2f
        : CruxMovement->MicroChargeTapImpulse;

    const FVector LungeDir = GetOwnerForward();

    // Replace velocity entirely — "committed linear momentum, you fly straight."
    // Z is zeroed on held lunge for flat trajectory. Preserved on tap so
    // gravity interacts naturally mid-air.
    FVector NewVelocity  = FVector::ZeroVector;
    NewVelocity         += LungeDir * ImpulseStrength;
    if (!bHeld) NewVelocity.Z = GetOwnerVelocity().Z;

    CruxMovement->Velocity = NewVelocity;

    // ── Held: lock trajectory (no steering, near-zero gravity) ────────────────
    if (bHeld && !bLungeLockActive)
    {
        bLungeLockActive = true;
        CruxMovement->GravityScale = 0.05f; // Nearly no arc during rail lunge
        CruxMovement->AirControl   = 0.f;

        GetWorldTimerManager().SetTimer(
            LungeLockTimerHandle,
            this, &ACruxSpear::EndLungeLock,
            LungeLockDuration,
            /*bLoop=*/false);
    }

    // ── Hit-resolution trace along the lunge path ─────────────────────────────
    // Multi-channel sweep catches both pawns AND world geometry in one pass,
    // so we know immediately whether to deal damage or trigger the wall-kick.
    FHitResult LungeHit;
    const FVector EyeLoc   = OwnerCharacter->GetPawnViewLocation();
    const FVector TraceEnd = EyeLoc + LungeDir * LungeTraceLength;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(OwnerCharacter);
    Params.AddIgnoredActor(this);

    const bool bHitSomething = GetWorld()->SweepSingleByObjectType(
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
//  Physics reflection off the wall normal:
//    1. Get incoming velocity (the lunge velocity at moment of impact)
//    2. Reflect off HitResult.Normal using V' = V - 2(V·N)N
//    3. Scale by WallKickMultiplier (exit faster than entry)
//    4. Enforce WallKickMinSpeed floor (even slow approaches yield useful kicks)
//    5. Apply with bAdditive=false (wall-kick IS the new trajectory)
//
//  Hitting perpendicular → bounce straight back.
//  Hitting at 45° → bounce at 45° to the other side.
//  Physics handles the geometry — no special-case angles needed.
//
// ============================================================================

void ACruxSpear::OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target)
{
    Super::OnFlowHook(HitResult, Target); // Guard

    // Wall-kick only fires on surface hits — enemy hits have no movement payoff
    if (Target != EFlowHookTarget::Surface) return;

    const FVector WallNormal  = HitResult.Normal;
    const FVector IncomingVel = GetOwnerVelocity();

    // Standard specular reflection: V' = V - 2(V·N)N
    const FVector ReflectedVel = IncomingVel.MirrorByVector(WallNormal);

    // Scale the kick — exit faster than entry speed
    FVector KickVelocity = ReflectedVel * WallKickMultiplier;

    // Enforce minimum speed floor so slow approaches still produce a useful kick
    if (KickVelocity.SizeSquared() < FMath::Square(WallKickMinSpeed))
        KickVelocity = KickVelocity.GetSafeNormal() * WallKickMinSpeed;

    // Replace velocity entirely — the wall-kick IS the new trajectory
    LaunchOwner(KickVelocity, /*bAdditive=*/false);

    // End any active lunge lock — the wall-kick restores player steering
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
    // Restore CMC defaults — match the CMC constructor values
    CruxMovement->GravityScale = 1.4f;
    CruxMovement->AirControl   = 0.6f;
}
