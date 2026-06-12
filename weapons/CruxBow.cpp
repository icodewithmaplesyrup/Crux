#include "headers/CruxBow.h"
#include "headers/CruxProjectile.h"
#include "../movement/CruxCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Kismet/KismetMathLibrary.h"

// ============================================================================
//  CONSTRUCTION
// ============================================================================

ACruxBow::ACruxBow()
{
    PrimaryActorTick.bCanEverTick = true;
    WeaponSlot        = EWeaponSlot::Slot3_Finisher;
    PrimaryDamage     = 18.f; // Per projectile; burst total is higher
    SecondaryDamage   = 5.f;  // Recoil blast is mostly physics, not damage
    FlowHookDamage    = 0.f;  // Flow Hook is a movement event, not a damage event
    MeleeTraceLength  = 0.f;  // No melee — ranged only
    WeaponDisplayName = FText::FromString(TEXT("Bow"));
}

void ACruxBow::BeginPlay()
{
    Super::BeginPlay();
}

void ACruxBow::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (PrimaryFireCDTimer > 0.f)
        PrimaryFireCDTimer = FMath::Max(0.f, PrimaryFireCDTimer - DeltaTime);

    // ── Bow draw timer ─────────────────────────────────────────────────────────
    // Count up while the bow is being drawn. If MaxDrawDuration is exceeded,
    // auto-fire so the hover window doesn't last indefinitely.
    if (bIsDrawingBow)
    {
        DrawTimer += DeltaTime;
        if (DrawTimer >= MaxDrawDuration)
        {
            // Auto-fire after max draw time
            SecondaryFire(/*bHeld=*/true);
        }
    }
}

// ============================================================================
//  PRIMARY FIRE — Burst spread
// ============================================================================
//
//  BOW VARIANT:
//    Pressing Primary starts the draw state (gravity reduced, player floats).
//    Releasing fires the burst. This is handled by BeginSecondaryFire / SecondaryFire.
//    When PRIMARY is pressed in Bow mode, we begin the draw NOT a burst.
//    (Input routing: Character should call BeginSecondaryFire on LMB press
//    and SecondaryFire on LMB release for Bow; or map them to separate input
//    handling in the Character class.)
//
//  CROSSBOW VARIANT:
//    Immediate burst fire on button press. Every shot applies CrossbowMicroShove
//    backward — the recoil stacks on rapid fire.
//
//  FLOW HOOK CHECK:
//    If the player is sprinting above FlowHookSprintThreshold and fires forward,
//    the Flow Hook fires — halting forward momentum and launching backward.
//    This is the "emergency brake / direction reversal."
//
// ============================================================================

void ACruxBow::PrimaryFire()
{
    Super::PrimaryFire();

    if (BowVariant == EBowVariant::Bow)
    {
        // Bow primary press = start drawing, not immediate fire.
        // The actual burst fires on release (handled via BeginSecondaryFire routing).
        // See header note on input routing.
        BeginSecondaryFire();
        return;
    }

    // ── Crossbow: immediate burst ────────────────────────────────────────────
    if (PrimaryFireCDTimer > 0.f) return;
    PrimaryFireCDTimer = PrimaryFireCooldown;

    FireBurst();

    // ── Crossbow micro-shove ──────────────────────────────────────────────────
    // Every shot pushes the player slightly backward. The accumulation of these
    // shoves across rapid fire is a deliberate design tool — shoot toward a wall
    // while running, drift backward safely, maintain range.
    const FVector BackwardPush = -GetOwnerForward() * CrossbowMicroShove;
    LaunchOwner(BackwardPush, /*bAdditive=*/true);

    // ── Flow Hook check ───────────────────────────────────────────────────────
    const float SpeedXY = GetOwnerVelocity().Size2D();
    if (SpeedXY > FlowHookSprintThreshold)
    {
        FHitResult DummyHit; // Flow Hook here is triggered by player speed, not a hit
        OnFlowHook(DummyHit, EFlowHookTarget::Miss); // Target=Miss signals "speed trigger"
    }
}

// ============================================================================
//  BOW DRAW BEGIN — Called on primary press (Bow variant)
// ============================================================================
//
//  When the player starts drawing the bow in mid-air, gravity is suppressed.
//  The player "floats" — slowly descending at DrawStateGravityScale.
//  This gives them the precision tracking window described in the GDD.
//
// ============================================================================

void ACruxBow::BeginSecondaryFire()
{
    if (!bIsEquipped || !OwnerCharacter || !CruxMovement) return;

    if (BowVariant == EBowVariant::Bow && !bIsDrawingBow)
    {
        bIsDrawingBow = true;
        DrawTimer     = 0.f;

        // Suppress gravity for the draw state hover window
        if (IsOwnerAirborne())
        {
            CruxMovement->GravityScale = DrawStateGravityScale;
        }
    }
}

// ============================================================================
//  SECONDARY FIRE — Recoil Blast (both variants) / Bow draw release
// ============================================================================
//
//  BOW: Called on primary RELEASE or Right Click.
//       Fires the burst, ends float state, restores gravity.
//
//  CROSSBOW: Right Click = Recoil Blast — always a violent backward launch.
//       "Violent Recoil Blast for escaping threat zones."
//
//  RECOIL BLAST physics:
//    The blast fires a short-range high-damage burst forward (minimal damage,
//    the point is the recoil). Then the player is launched BACKWARD at
//    RecoilBlastImpulse force. The backward component completely cancels and
//    reverses current forward velocity.
//
// ============================================================================

void ACruxBow::SecondaryFire(bool bHeld)
{
    Super::SecondaryFire(bHeld);

    if (BowVariant == EBowVariant::Bow && bIsDrawingBow)
    {
        // ── Bow: fire on release ──────────────────────────────────────────────
        FireBurst();
        EndDrawState();
    }
    else
    {
        // ── Recoil Blast ──────────────────────────────────────────────────────
        // CMC handles the micro-charge cooldown
        CruxMovement->FireMicroCharge(bHeld);

        // Short-range forward blast (minimal damage — the value is the recoil)
        // We don't use a physical projectile here — it's an instant-hit radial
        // effect representing the pressure wave.
        FHitResult BlastHit;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(OwnerCharacter);

        const FVector EyeLoc    = OwnerCharacter->GetPawnViewLocation();
        const FVector BlastEnd  = EyeLoc + GetOwnerForward() * 80.f; // Very short range

        GetWorld()->SweepSingleByChannel(
            BlastHit, EyeLoc, BlastEnd,
            FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeSphere(30.f), // Wide — pressure wave
            Params);

        if (BlastHit.bBlockingHit)
            ApplyWeaponDamage(BlastHit.GetActor(), SecondaryDamage, BlastHit);

        // ── Apply backward recoil to the player ───────────────────────────────
        //
        //  "Massive physics recoil on the player in the opposite direction."
        //  We replace XY velocity entirely with a backward impulse.
        //  Z is preserved — the recoil doesn't launch you up or down.
        //
        const FVector Backward        = -GetOwnerForward();
        FVector RecoilVelocity        = GetOwnerVelocity();
        RecoilVelocity.X              = Backward.X * RecoilBlastImpulse;
        RecoilVelocity.Y              = Backward.Y * RecoilBlastImpulse;
        // Z preserved: CruxMovement->Velocity.Z stays as-is
        RecoilVelocity.Z              = GetOwnerVelocity().Z;
        CruxMovement->Velocity        = RecoilVelocity;
    }
}

// ============================================================================
//  FLOW HOOK — Emergency Brake / Direction Reversal
// ============================================================================
//
//  Two trigger conditions:
//
//  1. SPEED-TRIGGERED (Crossbow primary above sprint threshold):
//     "Firing forward at full sprint completely halts forward momentum and
//     rockets the player backward."
//     Target=Miss signals this case (no physical hit, just speed check).
//
//     Implementation:
//     - Zero the forward component of velocity
//     - Apply BackwardRocketImpulse in the reverse of the horizontal velocity direction
//     - Keep Z so the player doesn't suddenly gain or lose height
//
//  2. SURFACE-TRIGGERED (Bow burst hits a surface):
//     Projectile impact on geometry functions as an anchor — the player is
//     pulled to a stop and can redirect toward the wall for a wall-run.
//     This is lower-priority and handled by the projectile's hit callback.
//     (Stub: connect ACruxProjectile's OnHit to this weapon's OnFlowHook.)
//
// ============================================================================

void ACruxBow::OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target)
{
    Super::OnFlowHook(HitResult, Target);

    if (Target == EFlowHookTarget::Miss)
    {
        // ── Speed-triggered emergency brake ───────────────────────────────────
        //
        //  The "brake" part: kill all forward velocity along the horizontal plane.
        //  The "rocket backward" part: apply a large backward impulse.
        //
        //  We decompose velocity into forward/right components, zero forward,
        //  then apply the backward blast. This preserves sideways drift and Z.
        //
        const FVector Forward    = GetOwnerForward();
        const FVector CurrentVel = GetOwnerVelocity();

        // Project current velocity onto the forward direction → how much is "forward"
        const float ForwardSpeed = FVector::DotProduct(CurrentVel, Forward);

        // If the player wasn't meaningfully moving forward, no brake effect
        if (ForwardSpeed < FlowHookSprintThreshold * 0.5f) return;

        // Remove the forward component, then add the backward blast
        FVector NewVelocity  = CurrentVel - (Forward * ForwardSpeed); // Zero forward component
        NewVelocity         += (-Forward) * RecoilBlastImpulse;       // Backward rocket

        CruxMovement->Velocity = NewVelocity;
    }
    // Surface-triggered Flow Hook: handled by ACruxProjectile::OnHit callback.
    // The projectile should call OwningWeapon->OnFlowHook(Hit, Surface) on impact.
}

// ============================================================================
//  BURST FIRE HELPER
// ============================================================================

void ACruxBow::FireBurst()
{
    if (PrimaryFireCDTimer > 0.f) return;
    PrimaryFireCDTimer = PrimaryFireCooldown;

    const FVector EyeLoc     = OwnerCharacter->GetPawnViewLocation();
    const FVector AimForward = OwnerCharacter->GetBaseAimRotation().Vector();

    // Spawn ProjectilesPerShot projectiles spread within PrimarySpreadAngle cone
    for (int32 i = 0; i < ProjectilesPerShot; ++i)
    {
        // Generate a random direction within the spread cone using UE's random unit vector
        const FVector SpreadDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(
            AimForward, PrimarySpreadAngle);

        FireProjectile(SpreadDir);
    }
}

void ACruxBow::FireProjectile(const FVector& Direction) const
{
    if (!OwnerCharacter || !GetWorld()) return;

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner      = OwnerCharacter;
    SpawnParams.Instigator = OwnerCharacter->GetInstigator();
    // AlwaysSpawn: don't abort if the muzzle clips geometry at spawn
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    const FVector SpawnLoc = OwnerCharacter->GetPawnViewLocation();

    ACruxProjectile* Proj = GetWorld()->SpawnActor<ACruxProjectile>(
        ACruxProjectile::StaticClass(),
        SpawnLoc,
        Direction.Rotation(),
        SpawnParams);

    if (Proj)
    {
        // Wire the bow back-reference so the projectile can call OnFlowHook on hit
        Proj->SetOwningBow(const_cast<ACruxBow*>(this));
        // Set per-projectile direction (applies the spread-cone offset)
        Proj->SetVelocityDirection(Direction);
        // Forward per-projectile damage from this weapon's PrimaryDamage
        Proj->HitDamage = PrimaryDamage;
    }
}

void ACruxBow::EndDrawState()
{
    bIsDrawingBow = false;
    DrawTimer     = 0.f;

    // Restore gravity
    if (CruxMovement)
        CruxMovement->GravityScale = 1.4f;
}
