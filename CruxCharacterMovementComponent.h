#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CruxCharacterMovementComponent.generated.h"

/**
 * HOW MOMENTUM LINKS TO CHARACTER MOVEMENT — READ THIS FIRST
 * ============================================================
 * UE5's CharacterMovementComponent drives the character via one core vector: Velocity.
 * Every tick, the CMC reads Velocity, applies acceleration/friction/gravity to it,
 * then calls MoveUpdatedComponent() which physically moves the capsule that amount.
 *
 * So the link is:
 *   Your code writes to Velocity  →  CMC moves the character that far this frame
 *
 * The key functions where we intercept this loop:
 *
 *   PhysCustom()         — Called every tick when MovementMode == MOVE_Custom.
 *                          This is where slide, wall-run, and wingsuit physics live.
 *                          You manipulate Velocity here directly.
 *
 *   OnMovementModeChanged() — Called whenever movement state changes (walking → sliding etc).
 *                             Use it to set up initial velocity for the new state.
 *
 *   CalcVelocity()       — Called during MOVE_Walking. We override it to prevent
 *                          UE's friction from killing momentum during a slide.
 *
 *   ProcessLanded()      — Called when the character hits the ground. Dash reset lives here.
 *
 * The Custom Movement Modes (ECustomMovementMode below) extend UE's built-in modes.
 * When you set MovementMode = MOVE_Custom and CustomMovementMode = CMOVE_Slide,
 * PhysCustom() is called with CustomMovementMode == CMOVE_Slide each tick, and
 * you handle the physics yourself. UE handles none of it — full control is yours.
 *
 * Velocity is in Unreal Units per second. At your 1:72 scale, tune speeds accordingly.
 */

UENUM(BlueprintType)
enum ECustomMovementMode
{
    CMOVE_Slide      UMETA(DisplayName = "Slide"),
    CMOVE_WallRun    UMETA(DisplayName = "WallRun"),
    CMOVE_Wingsuit   UMETA(DisplayName = "Wingsuit"),
    CMOVE_MAX        UMETA(Hidden),
};

UCLASS()
class CRUX_API UCruxCharacterMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()

public:
    UCruxCharacterMovementComponent();

    // -------------------------------------------------------------------------
    // SLIDE PARAMETERS
    // -------------------------------------------------------------------------

    /** Minimum speed required to initiate a slide (UU/s) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Slide")
    float SlideMinSpeed = 400.f;

    /** Friction applied while sliding — lower = longer, icier slides */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Slide")
    float SlideFriction = 0.15f;

    /** Gravity scale multiplier while sliding on a slope — amplifies downhill acceleration */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Slide")
    float SlideGravityScale = 2.5f;

    /** Impulse added to Velocity on slide-hop jump (forward boost) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Slide")
    float SlideHopImpulse = 600.f;

    /** How quickly capsule half-height lerps to crouched size during slide */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Slide")
    float SlideEnterSpeed = 10.f;

    // -------------------------------------------------------------------------
    // WALL-RUN PARAMETERS
    // -------------------------------------------------------------------------

    /** How far to trace sideways looking for a wall to run on (UU) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|WallRun")
    float WallRunTraceDistance = 75.f;

    /** Minimum horizontal speed to sustain a wall-run */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|WallRun")
    float WallRunMinSpeed = 350.f;

    /** Upward velocity applied when jumping off a wall */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|WallRun")
    float WallJumpOffForce = 550.f;

    /** How fast gravity pulls you off the wall (lower = stick longer) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|WallRun")
    float WallRunGravityScale = 0.25f;

    /**
     * Surface friction override for wall-run.
     * At runtime, this is populated from the ML surface classification.
     * Concrete default: 0.8 (controlled), Glass: 1.4 (fast/slippery).
     * Your ML pipeline writes to SurfaceFrictionOverride; this system reads it.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Crux|WallRun")
    float SurfaceFrictionOverride = 0.8f;

    // -------------------------------------------------------------------------
    // LEDGE MANTLE PARAMETERS
    // -------------------------------------------------------------------------

    /** Max height above the character's reach to attempt a mantle (UU) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mantle")
    float MantleMaxHeight = 120.f;

    /** Forward distance to check for a mantleable ledge */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mantle")
    float MantleReachDistance = 60.f;

    /** Speed at which the character is lerped up and over the ledge */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Mantle")
    float MantleSpeed = 350.f;

    // -------------------------------------------------------------------------
    // MICRO-CHARGE PARAMETERS
    // -------------------------------------------------------------------------

    /** Impulse magnitude for a tap micro-charge (quick burst) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|MicroCharge")
    float MicroChargeTapImpulse = 700.f;

    /** Impulse magnitude for a held micro-charge (committed momentum) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|MicroCharge")
    float MicroChargeHeldImpulse = 1400.f;

    /** Time in seconds the player must hold to trigger a held charge vs. tap */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|MicroCharge")
    float MicroChargeHoldThreshold = 0.3f;

    /** Cooldown between micro-charges (seconds) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|MicroCharge")
    float MicroChargeCooldown = 1.2f;

    // -------------------------------------------------------------------------
    // WINGSUIT PARAMETERS
    // -------------------------------------------------------------------------

    /** Gravity scale while wingsuit is active (near-zero = floaty glide) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Wingsuit")
    float WingsuitGravityScale = 0.1f;

    /** Max horizontal glide speed cap during wingsuit */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Wingsuit")
    float WingsuitMaxGlideSpeed = 1800.f;

    /** Forward drag applied each tick to prevent infinite glide acceleration */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Wingsuit")
    float WingsuitDrag = 0.05f;

    // -------------------------------------------------------------------------
    // DASH RESET
    // -------------------------------------------------------------------------

    /** Whether the dash is currently available (reset on specific landings) */
    UPROPERTY(BlueprintReadOnly, Category = "Crux|DashReset")
    bool bDashAvailable = true;

    // -------------------------------------------------------------------------
    // PUBLIC API — called by the Character or AbilitySystem
    // -------------------------------------------------------------------------

    /** Begin sliding. Call from Character on crouch-while-sprint input. */
    UFUNCTION(BlueprintCallable, Category = "Crux|Slide")
    void EnterSlide();

    /** End slide and return to walking. */
    UFUNCTION(BlueprintCallable, Category = "Crux|Slide")
    void ExitSlide();

    /** Called on jump input while sliding — converts momentum into a slide-hop. */
    UFUNCTION(BlueprintCallable, Category = "Crux|Slide")
    void SlideHop();

    /** Try to begin a wall-run. Returns true if a valid wall was found. */
    UFUNCTION(BlueprintCallable, Category = "Crux|WallRun")
    bool TryStartWallRun();

    /** Jump off current wall. */
    UFUNCTION(BlueprintCallable, Category = "Crux|WallRun")
    void WallJump();

    /** Try to mantle the ledge in front of the character. */
    UFUNCTION(BlueprintCallable, Category = "Crux|Mantle")
    bool TryMantle();

    /**
     * Fire a micro-charge in the input direction.
     * @param bHeld  true = player held past MicroChargeHoldThreshold
     */
    UFUNCTION(BlueprintCallable, Category = "Crux|MicroCharge")
    void FireMicroCharge(bool bHeld);

    /** Toggle wingsuit on/off (F key). */
    UFUNCTION(BlueprintCallable, Category = "Crux|Wingsuit")
    void ToggleWingsuit();

    /**
     * Called by the ML surface classification pipeline when the player
     * enters a new surface zone. Feeds into wall-run friction and slide speed.
     * @param InFriction  0.5 = ice/glass, 1.0 = concrete, 1.5+ = rough stone
     */
    UFUNCTION(BlueprintCallable, Category = "Crux|Surface")
    void SetSurfaceFriction(float InFriction);

    // UCharacterMovementComponent interface
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
    virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode,
        uint8 PreviousCustomMode) override;
    virtual float GetMaxSpeed() const override;

protected:
    // -------------------------------------------------------------------------
    // INTERNAL STATE
    // -------------------------------------------------------------------------

    bool bIsSliding      = false;
    bool bIsWallRunning  = false;
    bool bWingsuitActive = false;
    bool bIsMantling     = false;

    FVector WallRunNormal     = FVector::ZeroVector; // Normal of the wall we're running on
    FVector MantleTarget      = FVector::ZeroVector; // World position we're lerping to
    float   MicroChargeCDTimer = 0.f;                // Countdown to next available charge

    // -------------------------------------------------------------------------
    // PHYSICS IMPLEMENTATIONS
    // Called from PhysCustom() each tick for the matching custom mode
    // -------------------------------------------------------------------------

    /** Slide physics tick: apply slope gravity, friction, check exit conditions */
    void PhysSlide(float DeltaTime);

    /** Wall-run physics tick: cancel gravity, maintain speed, check wall still exists */
    void PhysWallRun(float DeltaTime);

    /** Wingsuit physics tick: low gravity, horizontal drag, dive-to-speed conversion */
    void PhysWingsuit(float DeltaTime);

    /** Mantle physics tick: lerp character to MantleTarget */
    void PhysMantle(float DeltaTime);

    // UCharacterMovementComponent overrides
    virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
    virtual void CalcVelocity(float DeltaTime, float Friction,
        bool bFluid, float BrakingDeceleration) override;
    virtual void ProcessLanded(const FHitResult& Hit, float remainingTime,
        int32 Iterations) override;

private:
    /** Shared helper: set our custom movement mode cleanly */
    void SetCustomMode(ECustomMovementMode NewMode);

    /** Check if we should exit the wall-run (wall gone, too slow, etc.) */
    bool ShouldExitWallRun() const;
};
