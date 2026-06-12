#include "headers/CruxWeaponBase.h"
#include "../movement/CruxCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

// The socket name on the Character's skeletal mesh that all weapons attach to.
// Must match the socket defined in your skeleton asset in the editor.
const FName ACruxWeaponBase::WeaponSocketName = TEXT("WeaponSocket_R");

// ============================================================================
//  CONSTRUCTION
// ============================================================================

ACruxWeaponBase::ACruxWeaponBase()
{
    PrimaryActorTick.bCanEverTick = false; // Weapons are event-driven, not tick-driven

    WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
    SetRootComponent(WeaponMesh);

    // Weapons are spawned at world origin and attached immediately.
    // Disable physics so they don't fall before Equip() runs.
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

    // Cast once and cache — subclass physics code writes `CruxMovement->...`
    // without repeated casting every frame.
    CruxMovement = Cast<UCruxCharacterMovementComponent>(
        NewOwnerCharacter->GetCharacterMovement());

    // Snap to the right-hand weapon socket on the character mesh.
    // SnapToTarget preserves socket-relative offsets set in the weapon Blueprint
    // (grip rotation tweaks, forward offset, etc.).
    FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, true);
    AttachToComponent(NewOwnerCharacter->GetMesh(), AttachRules, WeaponSocketName);

    WeaponMesh->SetVisibility(true);
    bIsEquipped = true;
}

void ACruxWeaponBase::Holster()
{
    WeaponMesh->SetVisibility(false);
    bIsEquipped = false;
    // Do NOT clear OwnerCharacter / CruxMovement — we may re-equip this slot.
    // Pointers are cleared only on actor destruction.
}

// ============================================================================
//  FIRE INTERFACE
//  Base implementations perform only the guard check, then return.
//  Subclasses call Super:: at the start to benefit from the guard, then proceed.
// ============================================================================

void ACruxWeaponBase::PrimaryFire()
{
    // Guard: weapon must be equipped and owner pointers valid
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
    if (!OwnerCharacter || !GetWorld()) return false;

    const float TraceLen = (OverrideLen > 0.f) ? OverrideLen : MeleeTraceLength;

    // Sweep from the character's eye location so the trace matches where the
    // player perceives the weapon tip to be at 1:72 scale.
    const FVector EyeLoc   = OwnerCharacter->GetPawnViewLocation();
    const FVector Forward  = OwnerCharacter->GetActorForwardVector();
    const FVector TraceEnd = EyeLoc + Forward * TraceLen;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(OwnerCharacter); // Don't hit self
    Params.AddIgnoredActor(this);

    if (MeleeTraceRadius > 0.f)
    {
        // Sphere sweep — catches targets even with imperfect aim (Sword, Mace)
        return GetWorld()->SweepSingleByChannel(
            OutHit, EyeLoc, TraceEnd,
            FQuat::Identity,
            ECC_Pawn,
            FCollisionShape::MakeSphere(MeleeTraceRadius),
            Params);
    }
    else
    {
        // Pure line trace — precision only (Spear primary)
        return GetWorld()->LineTraceSingleByChannel(
            OutHit, EyeLoc, TraceEnd, ECC_Pawn, Params);
    }
}

EFlowHookTarget ACruxWeaponBase::ClassifyHit(const FHitResult& Hit) const
{
    if (!Hit.bBlockingHit)
        return EFlowHookTarget::Miss;

    if (Hit.GetActor() && Hit.GetActor()->IsA(APawn::StaticClass()))
        return EFlowHookTarget::Enemy;

    return EFlowHookTarget::Surface;
}

void ACruxWeaponBase::ApplyWeaponDamage(AActor* Target, float Amount,
    const FHitResult& HitResult) const
{
    if (!Target || Amount <= 0.f) return;

    // ApplyPointDamage feeds the standard UE damage pipeline.
    // Connect a health component or GameplayAbility to the actor's TakeDamage event.
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
    // bAdditive=true  → bXYOverride=false, bZOverride=false → adds to existing velocity
    // bAdditive=false → bXYOverride=true,  bZOverride=true  → replaces velocity
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
