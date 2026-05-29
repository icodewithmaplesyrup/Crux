#pragma once

#include "CruxWeaponBase.h"
#include "CruxSpear.generated.h"

// ============================================================================
//  ACruxSpear — The Rail-Vault
//
//  GDD summary:
//    Primary Fire:  Rapid straight-forward thrust. Excellent reach.
//    Micro-Charge:
//      Tap  → Short forward burst thrust.
//      Hold → XL lunge — fully committed linear momentum. You fly straight.
//    Flow Hook: Missing a target and HITTING GEOMETRY does not produce
//               end-lag stall. Instead, the player instantly wall-kicks off
//               the surface at a sharp angle with a massive speed burst,
//               turning misses into repositions.
//
//  The Spear rewards distance management. The Flow Hook turns a whiffed lunge
//  into a reposition rather than a death sentence, making it forgiving in a
//  different way than the Sword — but only if you read the geometry.
// ============================================================================
UCLASS(BlueprintType, Blueprintable)
class CRUX_API ACruxSpear : public ACruxWeaponBase
{
    GENERATED_BODY()

public:
    ACruxSpear();

    virtual void PrimaryFire() override;
    virtual void ReleaseMicroCharge() override;
    virtual void TriggerFlowHook(const FCruxFlowHookContext& Context) override;

protected:
    // ── Primary Fire Config ───────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear")
    float ThrustRange = 350.f;

    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear")
    float ThrustDamage = 25.f;

    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear")
    float ThrustSphereRadius = 15.f; // Narrow trace — precision matters

    // ── Micro-Charge Config ───────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear|MicroCharge")
    float TapLungeImpulse = 1000.f;

    // Hold lunge: fully committed, overrides XY and Z for pure linear flight
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear|MicroCharge")
    float HeldLungeImpulse = 3200.f;

    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear|MicroCharge")
    float LungeDamage = 70.f;

    // How far forward to check for a hit at the end of the lunge
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear|MicroCharge")
    float LungeHitCheckRange = 400.f;

    // ── Flow Hook Config (Wall-Kick) ──────────────────────────────────────────

    // Speed of the wall-kick rebound. Higher = more punishing geometry required.
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear|FlowHook")
    float WallKickImpulse = 2200.f;

    // How much to angle the kick off the wall (0=pure reflection, 1=straight away)
    // ~0.5 gives the "sharp angle" described in the GDD
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Spear|FlowHook")
    float WallKickAngleBias = 0.5f;

    // Only geometry hits trigger the Flow Hook — not pawn hits
    // This bool tracks whether the lunge is currently active
    bool bLungeActive = false;
};
