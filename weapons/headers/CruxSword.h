#pragma once

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
//    Flow Hook: Hitting ANY target (enemy or physics object) resets gravity
//               for a split second — allowing a direct chain into wall-run
//               or another weapon's movement without losing forward speed.
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
    virtual void ReleaseMicroCharge() override;
    virtual void TriggerFlowHook(const FCruxFlowHookContext& Context) override;

protected:
    // ── Primary Fire Config ───────────────────────────────────────────────────

    // Horizontal damage arc half-angle (degrees). Wide enough to be forgiving.
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword")
    float SlashArcDegrees = 60.f;

    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword")
    float SlashDamage = 20.f;

    // How far the slash reaches
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword")
    float SlashRange = 180.f;

    // ── Micro-Charge Config ───────────────────────────────────────────────────

    // Tap: quick burst. Additive — preserves existing momentum.
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword|MicroCharge")
    float TapLaunchImpulse = 900.f;

    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword|MicroCharge")
    float TapSlashDamage = 30.f;

    // Hold: committed launch-slash. Huge speed, locked trajectory.
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword|MicroCharge")
    float HeldLaunchImpulse = 2400.f;

    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword|MicroCharge")
    float HeldSlashDamage = 60.f;

    // ── Flow Hook Config ──────────────────────────────────────────────────────

    // Duration (seconds) of the gravity-zero window after a hit
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Sword|FlowHook")
    float GravityResetDuration = 0.18f;

    // Saved gravity scale to restore after the window
    float SavedGravityScale = 1.4f;

    // Timer handle for restoring gravity
    FTimerHandle GravityRestoreTimer;

private:
    // Executes a multi-overlap arc sweep for the slash, damaging everything
    // in the horizontal cone. Separates geometry hits (Flow Hook eligible)
    // from pawn hits.
    void PerformSlashArc(float Damage, float Range);

    void RestoreGravity();
};
