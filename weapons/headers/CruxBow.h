#pragma once

#include "CoreMinimal.h"
#include "CruxWeaponBase.h"
#include "CruxBow.generated.h"

/**
 * THE BOW / CROSSBOW — The Recoil Engine
 * ========================================
 * Ranged utility acting as the "shotgun of the roster" — devastating up close,
 * useful for repositioning at any range. Two sub-variants share this class;
 * the variant is set via the BowVariant property.
 *
 * ── Primary Fire ────────────────────────────────────────────────────────────
 * Wide short-range burst-spread of projectiles. NOT a long-range sniper —
 * the spread makes it a close-range threat while requiring commitment to use
 * effectively. Think shotgun blast, not rifle.
 *
 * ── Secondary (Micro-Charge) — Kinetic Recoil Blast ─────────────────────────
 * "High-pressure kinetic blast — minimal damage, massive physics recoil
 * on the player in the OPPOSITE direction."
 *
 * This is inverted from the other weapons' secondaries. The value is the
 * movement of the PLAYER, not damage. The blast fires forward; the player
 * rockets backward. At full charge, this is the fastest single-moment velocity
 * change in the roster.
 *
 * ── Flow Hook — Emergency Brake / Direction Reversal ─────────────────────────
 * "Firing forward at full sprint completely halts forward momentum and rockets
 * the player backward. Functions as an emergency brake, direction reversal,
 * and ledge-cancellation tool."
 *
 * Implementation: when the player fires primary while above SprintThreshold
 * speed, ALL forward velocity is cancelled and a backward impulse is applied.
 * This is mechanically a "recoil" where the projectile pushes back instead of
 * the standard tiny physics nudge.
 *
 * ── Bow Variant: Float State ──────────────────────────────────────────────────
 * "Pulling back the bowstring slows descent — a brief float state mid-air
 * enabling precision tracking of targets while airborne from a slide-hop."
 *
 * Holding Primary (not releasing) while airborne reduces GravityScale to near
 * zero. Releasing fires the arrow burst and restores gravity instantly.
 * This creates a "hover window" for aiming — high skill expression.
 *
 * ── Crossbow Variant: Micro-Shove ─────────────────────────────────────────────
 * "Every shot provides a tiny micro-shove in the opposite direction."
 * Each primary fire gives a small backward impulse, compounding across rapid fire.
 * Secondary = Recoil Blast (same as base, but more violent).
 */

UENUM(BlueprintType)
enum class EBowVariant : uint8
{
    Bow       UMETA(DisplayName = "Bow — Float State"),
    Crossbow  UMETA(DisplayName = "Crossbow — Micro-Shove + Recoil Blast"),
};

UCLASS(BlueprintType, Blueprintable)
class CRUX_API ACruxBow : public ACruxWeaponBase
{
    GENERATED_BODY()

public:
    ACruxBow();

    // -------------------------------------------------------------------------
    // VARIANT SELECTION
    // -------------------------------------------------------------------------

    /**
     * Which variant this instance is. Set on the CDO in Blueprint.
     * Bow and Crossbow share all physics logic; only the primary fire behavior
     * and the float-state differ between them.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crux|Bow")
    EBowVariant BowVariant = EBowVariant::Bow;

    // -------------------------------------------------------------------------
    // SHARED PARAMETERS
    // -------------------------------------------------------------------------

    /**
     * Number of projectiles spawned per primary fire burst.
     * These fire in a spread pattern (see PrimarySpreadAngle).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Bow")
    int32 ProjectilesPerShot = 5;

    /**
     * Half-angle of the projectile spread cone (degrees).
     * Higher = wider shotgun spread.
     * At 1:72 scale, a wide spread at close range hits everything nearby.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Bow")
    float PrimarySpreadAngle = 12.f;

    /** Speed of spawned projectiles (UU/s) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Bow")
    float ProjectileSpeed = 3000.f;

    /** Cooldown between primary fire bursts (seconds) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Bow")
    float PrimaryFireCooldown = 0.5f;

    /**
     * Backward impulse applied to the player per primary shot.
     * Bow: only fires when Flow Hook condition is met (above sprint threshold).
     * Crossbow: applied on EVERY shot (micro-shove).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Bow")
    float PrimaryRecoilImpulse = 120.f;

    /**
     * Backward impulse for the secondary Recoil Blast.
     * This is the "massive physics recoil" — should feel violent.
     * Much larger than the primary micro-shove.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Bow")
    float RecoilBlastImpulse = 1800.f;

    /**
     * Horizontal speed threshold above which the Flow Hook fires on primary shot.
     * If the player is sprinting faster than this (UU/s), firing forward
     * triggers the emergency brake direction reversal.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Bow")
    float FlowHookSprintThreshold = 500.f;

    // ── Bow Variant Only ──────────────────────────────────────────────────────

    /**
     * GravityScale applied while the bowstring is being held (Bow variant only).
     * Near-zero = near-float. The player very slowly descends during the draw.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Bow|BowVariant")
    float DrawStateGravityScale = 0.05f;

    /**
     * Maximum duration the bowstring can be held before auto-firing (seconds).
     * Prevents players from hovering indefinitely.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Bow|BowVariant")
    float MaxDrawDuration = 1.5f;

    // ── Crossbow Variant Only ─────────────────────────────────────────────────

    /**
     * Micro-shove impulse per shot for the Crossbow variant.
     * Separate from PrimaryRecoilImpulse so Bow and Crossbow can tune independently.
     * Stacks across rapid fire — running backward while shooting is intentional.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Bow|CrossbowVariant")
    float CrossbowMicroShove = 180.f;

    // -------------------------------------------------------------------------
    // OVERRIDES
    // -------------------------------------------------------------------------

    virtual void PrimaryFire() override;

    /** Called on Right Click press — begins draw (Bow) or fires burst (Crossbow) */
    UFUNCTION(BlueprintCallable, Category = "Crux|Bow")
    void BeginSecondaryFire();

    /** Called on Right Click release — fires arrow (Bow) or Recoil Blast (Crossbow) */
    virtual void SecondaryFire(bool bHeld) override;

    virtual void OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target) override;

private:
    float PrimaryFireCDTimer = 0.f;
    bool  bIsDrawingBow      = false;   // Bow: true while holding primary
    float DrawTimer          = 0.f;     // Bow: time spent drawing

    FTimerHandle AutoFireTimerHandle;

    /**
     * Spawn a single projectile in the given direction.
     * Spawns an ACruxProjectile (forward-declared below) actor.
     * @param Direction  World-space normalized direction to fire
     */
    void FireProjectile(const FVector& Direction) const;

    /**
     * Fire the full burst spread for primary fire.
     * Generates ProjectilesPerShot directions within PrimarySpreadAngle cone.
     */
    void FireBurst();

    /** Restore gravity after Bow draw is released or auto-fires */
    void EndDrawState();

    virtual void Tick(float DeltaTime) override;
    virtual void BeginPlay() override;
};
