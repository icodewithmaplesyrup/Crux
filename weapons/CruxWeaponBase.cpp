#include "CruxWeaponBase.h"
#include "CruxCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

// The socket name defined on the Character's skeletal mesh in the editor.
// All weapons attach here when equipped. Must match the socket in your skeleton.
const FName ACruxWeaponBase::WeaponSocketName = TEXT("WeaponSocket_R");

// ============================================================================
//  CONSTRUCTION
// ============================================================================

ACruxWeaponBase::ACruxWeaponBase()
{
    PrimaryActorTick.bCanEverTick = false; // Weapons don't tick — events only

    WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
    SetRootComponent(WeaponMesh);

    // Weapons are spawned and immediately attached — disable physics simulation
    // so they don't fall before Equip() is called.
    WeaponMesh->SetSimulatePhysics(false);
    WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACruxWeaponBase::BeginPlay()
{
    Super::BeginPlay();
    // Weapons spawn holstered. HotbarComponent calls Equip() on the default slot.
    WeaponMesh->SetVisibility(false);
}

// ============================================================================
//  EQUIP / HOLSTER
// ============================================================================

void ACruxWeaponBase::Equip(ACharacter* NewOwnerCharacter)
{
    if (!NewOwnerCharacter) return;

    OwnerCharacter = NewOwnerCharacter;

    // Resolve and cache the CMC. Cast once here so subclass physics code
    // can just write `CruxMovement->...` without repeated casting.
    CruxMovement = Cast<UCruxCharacterMovementComponent>(
        NewOwnerCharacter->GetCharacterMovement());

    // Attach directly to the weapon socket on the character mesh.
    // SnapToTargetNotIncludingScale preserves socket-relative offsets you set
    // in the weapon Blueprint (rotation tweaks, grip offset, etc.)
    FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, true);
    AttachToComponent(NewOwnerCharacter->GetMesh(), AttachRules, WeaponSocketName);

    WeaponMesh->SetVisibility(true);
    bIsEquipped = true;
}

void ACruxWeaponBase::Holster()
{
    WeaponMesh->SetVisibility(false);
    bIsEquipped = false;
    // Do NOT clear OwnerCharacter/CruxMovement — we may re-equip this slot.
    // They're cleared only on actor destruction.
}

// ============================================================================
//  FIRE INTERFACE — base implementations do nothing
//  Subclasses override these. Calling Super is optional but recommended for
//  guard checks (bIsEquipped, ensure owner valid).
// ============================================================================

void ACruxWeaponBase::PrimaryFire()
{
    // Guard: weapon must be equipped and owner valid
    if (!bIsEquipped || !OwnerCharacter || !CruxMovement) return;
}

void ACruxWeaponBase::SecondaryFire(bool bHeld)
{
    if (!bIsEquipped || !OwnerCharacter || !CruxMovement) return;
}

void ACruxWeaponBase::OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target)
{
    if (!bIsEquipped || !OwnerCharacter || !CruxMovement) return;
}

// ============================================================================
//  SHARED HELPERS
// ============================================================================

bool ACruxWeaponBase::MeleeSweep(FHitResult& OutHit, float OverrideLen) const
{
    if (!OwnerCharacter) return false;

    const float TraceLen = (OverrideLen > 0.f) ? OverrideLen : MeleeTraceLength;

    // Start the sweep from the character's eye location — matches where the
    // player perceives the weapon tip to be at 1:72 scale.
    const FVector EyeLoc   = OwnerCharacter->GetPawnViewLocation();
    const FVector Forward  = OwnerCharacter->GetActorForwardVector();
    const FVector TraceEnd = EyeLoc + Forward * TraceLen;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(OwnerCharacter); // Don't hit self
    Params.AddIgnoredActor(this);

    if (MeleeTraceRadius > 0.f)
    {
        // Sphere sweep — catches targets even with imperfect aim
        return GetWorld()->SweepSingleByChannel(
            OutHit, EyeLoc, TraceEnd,
            FQuat::Identity,
            ECC_Pawn,
            FCollisionShape::MakeSphere(MeleeTraceRadius),
            Params);
    }
    else
    {
        // Pure line trace — use for very precise weapons (Spear secondary)
        return GetWorld()->LineTraceSingleByChannel(
            OutHit, EyeLoc, TraceEnd, ECC_Pawn, Params);
    }
}

EFlowHookTarget ACruxWeaponBase::ClassifyHit(const FHitResult& Hit) const
{
    if (!Hit.bBlockingHit)
        return EFlowHookTarget::Miss;

    // Pawn actors are enemies (or friendlies — damage system handles team checks)
    if (Hit.GetActor() && Hit.GetActor()->IsA(APawn::StaticClass()))
        return EFlowHookTarget::Enemy;

    return EFlowHookTarget::Surface;
}

void ACruxWeaponBase::ApplyWeaponDamage(AActor* Target, float Amount,
    const FHitResult& HitResult) const
{
    if (!Target || Amount <= 0.f) return;

    // UGameplayStatics::ApplyPointDamage feeds into the standard UE damage pipeline.
    // Connect your GameplayAbility or health component to the TakeDamage event.
    UGameplayStatics::ApplyPointDamage(
        Target,
        Amount,
        GetOwnerForward(),
        HitResult,
        OwnerCharacter ? OwnerCharacter->GetController() : nullptr,
        this,
        nullptr // DamageType — swap in UCruxDamageType once created
    );
}

void ACruxWeaponBase::LaunchOwner(const FVector& Impulse, bool bAdditive) const
{
    if (!OwnerCharacter) return;

    // LaunchCharacter(Impulse, bXYOverride, bZOverride):
    // When bAdditive=true, we want ADDITIVE behavior — pass false for both overrides
    // so UE adds to existing velocity rather than replacing it.
    // When bAdditive=false (flow hooks that need precise redirects), we pass true
    // to replace X/Y/Z components as needed.
    OwnerCharacter->LaunchCharacter(Impulse, !bAdditive, !bAdditive);
}

FVector ACruxWeaponBase::GetOwnerForward() const
{
    return OwnerCharacter ? OwnerCharacter->GetActorForwardVector() : FVector::ForwardVector;
}

FVector ACruxWeaponBase::GetOwnerVelocity() const
{
    return CruxMovement ? CruxMovement->Velocity : FVector::ZeroVector;
}

bool ACruxWeaponBase::IsOwnerAirborne() const
{
    return CruxMovement ? CruxMovement->IsFalling() : false;
}
