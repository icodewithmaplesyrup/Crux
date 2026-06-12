#pragma once
#include "../../CruxCompatibility.h"

#include "CruxWeaponBase.h"
#include "CruxSpear.generated.h"

// ============================================================================
//  ACruxSpear — The Rail-Vault
//
//  GDD summary:
//    Primary Fire:  Rapid straight-forward thrust. Excellent reach, narrow hitbox.
//    Micro-Charge:
//      Tap  → Short forward burst thrust. Preserves Z.
//      Hold → XL lunge — fully committed linear momentum. Gravity suppressed.
//             You fly straight. No steering. No arc.
//    Flow Hook: Missing a target and HITTING GEOMETRY does not produce end-lag.
//               Instead the player wall-kicks off the surface at the mirror angle
//               of approach with a massive speed burst — turning misses into
//               repositions.
//
//  The Spear rewards distance management. The Flow Hook turns a whiffed lunge
//  into a reposition rather than a death sentence — but only if you read geometry.
// ============================================================================
UCLASS(BlueprintType, Blueprintable)
class CRUX_API ACruxSpear : public ACruxWeaponBase
{
    GENERATED_BODY()

public:
    ACruxSpear();

    virtual void PrimaryFire() override;
    virtual void SecondaryFire(bool bHeld) override;
    virtual void OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target) override;

protected:
    // ── Primary Fire Config ───────────────────────────────────────────────────

    /** Seconds between primary thrusts. Lower than Sword — it's a poke weapon. */
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear")
    float ThrustCooldown = 0.18f;

    // ── Lunge / Micro-Charge Config ───────────────────────────────────────────

    /**
     * How long (seconds) the held-lunge trajectory lock lasts.
     * During lock: AirControl=0, GravityScale≈0. Player is a rail.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear|MicroCharge")
    float LungeLockDuration = 0.4f;

    /**
     * How far forward the hit-detection trace reaches at the end of the lunge.
     * Longer than Sword because the Spear's XL lunge covers more distance.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear|MicroCharge")
    float LungeTraceLength = 500.f;

    // ── Flow Hook Config (Wall-Kick) ──────────────────────────────────────────

    /**
     * Speed multiplier applied to the reflected velocity on wall-kick.
     * >1 = the kick exits faster than the incoming lunge speed.
     * 1.3 gives a strong "bonus speed" reward for reading geometry well.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear|FlowHook")
    float WallKickMultiplier = 1.3f;

    /**
     * Minimum exit speed after a wall-kick (UU/s).
     * Prevents a near-stationary lunge from producing a useless tiny kick.
     * Even a slow approach yields a meaningful reposition.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear|FlowHook")
    float WallKickMinSpeed = 800.f;

private:
    // ── Runtime timers ────────────────────────────────────────────────────────

    /** Counts down from ThrustCooldown each frame. */
    float ThrustCDTimer = 0.f;

    /** True while the held-lunge trajectory lock is active. */
    bool bLungeLockActive = false;

    FTimerHandle LungeLockTimerHandle;

    // ── Timer callbacks ───────────────────────────────────────────────────────

    /** Restores GravityScale and AirControl after the lunge lock expires. */
    void EndLungeLock();

    // ── Tick (needed for ThrustCDTimer countdown) ─────────────────────────────

    virtual void Tick(float DeltaTime) override;
    virtual void BeginPlay() override;
};
