#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CruxProjectile.generated.h"

class ACruxBow;
class UProjectileMovementComponent;
class USphereComponent;

/**
 * ACruxProjectile
 * ================
 * Simple physics projectile spawned by the Bow's FireBurst().
 *
 * Lifecycle:
 *   1. Spawned by ACruxBow::FireProjectile()
 *   2. OwningBow is set immediately after spawn
 *   3. ProjectileMovement drives it forward at ProjectileInitialSpeed
 *   4. OnHit fires on first blocking collision
 *      - Pawn hit  → deal damage, call OwningBow->OnFlowHook(Surface) only if
 *                    the bow's surface-triggered flow hook is relevant
 *      - Geometry  → call OwningBow->OnFlowHook(Surface) for the anchor/redirect
 *   5. Actor destroys itself after MaxLifetime seconds or on first hit
 *
 * Design note:
 *   The Flow Hook callback path (projectile → bow → player velocity) is what
 *   enables the Bow's surface-triggered Flow Hook described in the GDD:
 *   "projectile anchors to surface, player is pulled to a halt and can redirect
 *   toward the wall for a wall-run."
 */
UCLASS(BlueprintType, Blueprintable)
class CRUX_API ACruxProjectile : public AActor
{
    GENERATED_BODY()

public:
    ACruxProjectile();

    /**
     * Set the weapon that fired this projectile.
     * Must be called immediately after spawn — OnHit uses this to call back.
     */
    UFUNCTION(BlueprintCallable, Category = "Crux|Projectile")
    void SetOwningBow(ACruxBow* InBow) { OwningBow = InBow; }

    // ── Config ─────────────────────────────────────────────────────────────────

    /** Initial speed in UU/s. Set by FireProjectile() via SetVelocityDirection(). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crux|Projectile")
    float ProjectileInitialSpeed = 3000.f;

    /** Damage dealt to a pawn on direct hit. Forwarded from ACruxBow::PrimaryDamage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Projectile")
    float HitDamage = 18.f;

    /** How long (seconds) before the projectile auto-destroys if it hits nothing. */
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Projectile")
    float MaxLifetime = 3.f;

    /**
     * Set the initial velocity direction. Called by ACruxBow::FireProjectile()
     * right after spawn — this is how the spread cone is applied per-projectile.
     */
    UFUNCTION(BlueprintCallable, Category = "Crux|Projectile")
    void SetVelocityDirection(const FVector& Direction);

protected:
    virtual void BeginPlay() override;

private:
    // ── Components ─────────────────────────────────────────────────────────────

    /** Collision sphere — narrow for arrow-like feel */
    UPROPERTY(VisibleAnywhere, Category = "Crux|Projectile")
    USphereComponent* CollisionSphere = nullptr;

    /** UE's built-in projectile movement — handles gravity, bouncing, speed */
    UPROPERTY(VisibleAnywhere, Category = "Crux|Projectile")
    UProjectileMovementComponent* ProjectileMovement = nullptr;

    // ── Runtime ────────────────────────────────────────────────────────────────

    UPROPERTY()
    ACruxBow* OwningBow = nullptr;

    // ── Collision callback ─────────────────────────────────────────────────────

    /**
     * Fires on the first blocking hit.
     * - Pawn → deal damage via the standard damage pipeline
     * - Geometry → call OwningBow->OnFlowHook(Hit, Surface) for the anchor redirect
     * Destroys self after handling the hit.
     */
    UFUNCTION()
    void OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
               UPrimitiveComponent* OtherComponent, FVector NormalImpulse,
               const FHitResult& Hit);
};
