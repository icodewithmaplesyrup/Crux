#include "CruxProjectile.h"
#include "CruxBow.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"

// ============================================================================
//  CONSTRUCTION
// ============================================================================

ACruxProjectile::ACruxProjectile()
{
    PrimaryActorTick.bCanEverTick = false; // ProjectileMovement handles motion

    // ── Collision sphere ───────────────────────────────────────────────────────
    // Small radius — arrows should feel precise, not like grenades.
    CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
    CollisionSphere->InitSphereRadius(4.f);
    CollisionSphere->SetCollisionProfileName(TEXT("Projectile"));
    SetRootComponent(CollisionSphere);

    CollisionSphere->OnComponentHit.AddDynamic(this, &ACruxProjectile::OnHit);

    // ── Projectile movement ────────────────────────────────────────────────────
    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(
        TEXT("ProjectileMovement"));
    ProjectileMovement->UpdatedComponent  = CollisionSphere;
    ProjectileMovement->InitialSpeed      = ProjectileInitialSpeed;
    ProjectileMovement->MaxSpeed          = ProjectileInitialSpeed;
    ProjectileMovement->bRotationFollowsVelocity = true;  // Arrow points forward
    ProjectileMovement->bShouldBounce     = false;        // Projectiles don't bounce
    ProjectileMovement->ProjectileGravityScale = 0.3f;    // Slight arc — not rail-straight
}

void ACruxProjectile::BeginPlay()
{
    Super::BeginPlay();

    // Auto-destroy after MaxLifetime seconds — prevents orphaned arrows accumulating
    SetLifeSpan(MaxLifetime);
}

// ============================================================================
//  VELOCITY DIRECTION
//  Called by ACruxBow::FireProjectile() immediately after spawn.
//  Sets velocity in the spread-cone direction for this projectile.
// ============================================================================

void ACruxProjectile::SetVelocityDirection(const FVector& Direction)
{
    if (ProjectileMovement)
    {
        ProjectileMovement->Velocity = Direction.GetSafeNormal() * ProjectileInitialSpeed;

        // Rotate the actor to face the flight direction so the mesh points correctly
        SetActorRotation(Direction.Rotation());
    }
}

// ============================================================================
//  ON HIT — the callback that closes the projectile → bow → player loop
// ============================================================================
//
//  Two paths:
//
//  1. PAWN HIT: Deal damage. No Flow Hook call — the Bow's projectile hitting
//     a pawn is just a damage event. The Bow's surface-triggered Flow Hook is
//     specifically about geometry (anchor/redirect), not enemy hits.
//
//  2. GEOMETRY HIT: Call OwningBow->OnFlowHook(Hit, Surface).
//     This is the "surface-triggered Flow Hook" described in the GDD:
//     the projectile anchors to the wall and the bow uses that to redirect
//     the player's momentum for a wall-run setup. The exact physics response
//     is implemented in ACruxBow::OnFlowHook — this just delivers the hit data.
//
//  In both cases the projectile destroys itself on hit (no penetration).
//
// ============================================================================

void ACruxProjectile::OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
    UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!OtherActor || OtherActor == GetInstigator()) return;

    const bool bHitPawn = OtherActor->IsA(APawn::StaticClass());

    if (bHitPawn)
    {
        // ── Pawn hit: deal damage ─────────────────────────────────────────────
        UGameplayStatics::ApplyPointDamage(
            OtherActor,
            HitDamage,
            GetActorForwardVector(),
            Hit,
            GetInstigator() ? GetInstigator()->GetController() : nullptr,
            this,
            nullptr // DamageType — swap in UCruxDamageType once created
        );
        // No Flow Hook for pawn hits — value was the damage
    }
    else if (OwningBow)
    {
        // ── Geometry hit: fire the surface-triggered Flow Hook ─────────────────
        // The bow receives the hit result and decides how to redirect the player.
        // At the time of writing, OnFlowHook with Target=Surface halts forward
        // momentum and sets up a wall-run entry direction.
        OwningBow->OnFlowHook(Hit, EFlowHookTarget::Surface);
    }

    // Destroy self regardless — no penetration
    Destroy();
}
