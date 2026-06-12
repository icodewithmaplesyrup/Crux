#pragma once
#include "../../CruxCompatibility.h"

#include "CruxWeaponBase.h"
#include "CruxSword.generated.h"

// ============================================================================
//  ACruxSword — The Momentum Slider
//
//  GDD summary:
//    Primary Fire:  Fast horizontal slash. Reliable, button-mash viable.
//    Micro-Charge:
//      Tap  → Quick forward burst-slash (short range, moderate speed)
//      Hold → High-speed launch-slash. Commits trajectory, massive speed.
//    Flow Hook: Hitting ANY target (enemy or geometry) resets gravity for a
//               split second — allowing a direct chain into wall-run or
//               another weapon's movement without losing forward speed.
//
//  This is the RECOMMENDED STARTING WEAPON — its Flow Hook is the most
//  forgiving because it triggers on any successful hit, not a positional one.
// ============================================================================
UCLASS(BlueprintType, Blueprintable)
class CRUX_API ACruxSword : public ACruxWeaponBase
{
    GENERATED_BODY()

public:
    ACruxSword();

    virtual void PrimaryFire() override;
    virtual void SecondaryFire(bool bHeld) override;
    virtual void OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target) override;

protected:
    // ── Primary Fire Config ───────────────────────────────────────────────────

    /** Rate limit on primary slashes (seconds between swings). */
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword")
    float PrimarySlashCooldown = 0.22f;

    // ── Lunge / Micro-Charge Config ───────────────────────────────────────────

    /**
     * Gravity scale applied during a held lunge so the character flies level.
     * Near-zero = nearly horizontal flight. Restored by EndLungeLock().
     */
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword|MicroCharge")
    float LungeGravityScale = 0.05f;

    /**
     * How long (seconds) the trajectory lock lasts on a held lunge.
     * During this window: AirControl=0, GravityScale=LungeGravityScale.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword|MicroCharge")
    float LungeLockDuration = 0.35f;

    // ── Flow Hook Config ──────────────────────────────────────────────────────

    /**
     * How long (seconds) gravity is zeroed after a successful hit.
     * During this window: player floats, micro-charge cooldown is reset.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword|FlowHook")
    float SwordFlowHookDuration = 0.18f;

private:
    // ── Runtime timers ────────────────────────────────────────────────────────

    /** Counts down from PrimarySlashCooldown each frame. */
    float PrimarySlashCDTimer = 0.f;

    /** True while the held-lunge trajectory lock is active. */
    bool bLungeLockActive = false;

    /** True while the Flow Hook gravity-zero window is active. */
    bool bFlowHookWindowActive = false;

    FTimerHandle LungeLockTimerHandle;
    FTimerHandle FlowHookTimerHandle;

    // ── Timer callbacks ───────────────────────────────────────────────────────

    /** Restores GravityScale and AirControl after the lunge lock expires. */
    void EndLungeLock();

    /** Restores GravityScale after the Flow Hook window expires. */
    void EndFlowHookWindow();

    // ── Tick (needed for PrimarySlashCDTimer countdown) ──────────────────────

    virtual void Tick(float DeltaTime) override;
    virtual void BeginPlay() override;
};
