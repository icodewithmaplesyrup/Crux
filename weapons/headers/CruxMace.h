#pragma once

#include "CoreMinimal.h"
#include "../../CruxCompatibility.h"
#include "CruxWeaponBase.h"
#include "CruxMace.generated.h"

/**
 * THE MACE / AXE — The Rocket-Stomper
 * =====================================
 * Heavy, high-impact gravity manipulator. Built for vertical dominance and
 * area denial. The Mace is the highest skill expression weapon for players
 * who think vertically — every stomp is a movement decision.
 *
 * ── Primary Fire ────────────────────────────────────────────────────────────
 * Slow, devastating overhead slam. Generates a small physics shockwave on
 * impact that sends nearby loose physics debris flying as chaotic hazards.
 * The slam itself deals high damage on direct hit; the shockwave is secondary.
 *
 * ── Secondary — Grounded: Rocket Jump ───────────────────────────────────────
 * "Downward blast functioning as a vertical rocket jump."
 * Launches the player straight up with the highest vertical displacement in
 * the game. Costs a chunk of health if implemented with realistic rocket-jump
 * self-damage (optional — tunable). This is the main vertical access tool.
 *
 * ── Secondary — Airborne: Dive-Bomb Stomp ────────────────────────────────────
 * "Sudden high-velocity dive-bomb stomp straight to the ground."
 * When airborne, secondary fires downward — kills all current Velocity.XY,
 * applies massive downward acceleration, and ends in a slam shockwave on landing.
 * The slam shockwave on landing is LARGER than the primary's shockwave.
 *
 * ── Flow Hook — The Sky Crusher ──────────────────────────────────────────────
 * "Landing a mid-air stomp directly on top of an enemy crushes them instantly
 * and launches the player back into the sky with doubled vertical height."
 *
 * Implementation: when the dive-bomb stomp's downward trace hits a Pawn actor,
 * the enemy takes crush damage and is killed instantly (or takes massive damage
 * if insta-kill is disabled), and the player is immediately launched upward
 * with 2× the vertical component of a standard rocket jump.
 *
 * This creates a vertical "bounce-kill" loop: stomp → kill → sky → stomp again.
 * The highest-skill play pattern in the launch roster.
 */

UCLASS(BlueprintType, Blueprintable)
class CRUX_API ACruxMace : public ACruxWeaponBase
{
    GENERATED_BODY()

public:
    ACruxMace();

    // -------------------------------------------------------------------------
    // MACE-SPECIFIC PARAMETERS
    // -------------------------------------------------------------------------

    /** Time between primary overhead slams (seconds). High to emphasize weight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mace")
    float SlamCooldown = 0.7f;

    /**
     * Radius of the physics shockwave spawned on primary slam impact (UU).
     * Objects within this sphere receive a radial outward impulse.
     * At 1:72 scale, 150 UU ≈ 2 real inches = a 12-foot area.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mace")
    float SlamShockwaveRadius = 150.f;

    /** Force magnitude applied to physics objects within the shockwave radius */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mace")
    float SlamShockwaveForce = 2000.f;

    /** Vertical impulse for the rocket jump secondary (grounded) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mace")
    float RocketJumpUpForce = 1600.f;

    /** Optional self-damage from rocket jump. 0 = no self-damage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mace")
    float RocketJumpSelfDamage = 0.f;

    /** Downward velocity applied when the dive-bomb begins (UU/s) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mace")
    float DiveBombDownVelocity = 2200.f;

    /**
     * Upward launch force applied on a successful Sky Crusher Flow Hook kill.
     * 2× the rocket jump base force as stated in the GDD.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mace")
    float SkyCrusherBounceForce = 3200.f;

    /** Damage applied to a pawn hit by the dive-bomb Sky Crusher */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mace")
    float SkyCrusherDamage = 999.f; // Intentional insta-kill default — tune if needed

    // -------------------------------------------------------------------------
    // OVERRIDES
    // -------------------------------------------------------------------------

    virtual void PrimaryFire() override;
    virtual void SecondaryFire(bool bHeld) override;
    virtual void OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target) override;

private:
    float SlamCDTimer      = 0.f;
    bool  bDiveBombActive  = false; // True while diving — used to trigger landing logic

    /**
     * Broadcast a radial physics impulse at the impact point.
     * Used by both primary slam and the dive-bomb landing.
     * @param Origin   World position of the impact
     * @param Radius   Sphere radius to affect
     * @param Force    Impulse magnitude
     */
    void SpawnShockwave(const FVector& Origin, float Radius, float Force) const;

    virtual void Tick(float DeltaTime) override;
    virtual void BeginPlay() override;
};
