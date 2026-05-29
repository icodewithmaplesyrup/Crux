#include "CruxCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h" // Remove in shipping — used for wall-run trace visualization
#include "Engine/World.h"
#include "TimerManager.h"

// ============================================================================
//  THE MOMENTUM → MOVEMENT LINK, EXPLAINED IN CODE
// ============================================================================
//
//  Every frame UE runs this loop (simplified):
//
//    PerformMovement(DeltaTime)
//      └─ CalcVelocity()          ← UE applies acceleration + friction to Velocity
//      └─ PhysWalking() / PhysCustom() / etc.
//           └─ MoveUpdatedComponent(Velocity * DeltaTime)   ← actually moves capsule
//
//  So "momentum" is just Velocity. If you write:
//      Velocity = FVector(1000, 0, 0);
//  ...the character moves 1000 UU/s in X. That's the entire link.
//
//  The subtlety is WHEN you write to Velocity:
//  - In PhysCustom(): you own the whole tick. UE won't touch Velocity before moving.
//  - Adding an impulse (LaunchCharacter / AddImpulse): safe from anywhere, adds instantly.
//  - In CalcVelocity(): you can modify what UE was about to apply.
//
//  The override strategy here:
//  - Slide / WallRun / Wingsuit → MOVE_Custom → PhysCustom() → we write Velocity directly
//  - Mantle → we lerp the actor position via MoveUpdatedComponent with a computed delta
//  - SlideHop / WallJump → impulse added via LaunchCharacter (additive, frame-safe)
//  - MicroCharge → direct Velocity write + LaunchCharacter for the Z component
//
// ============================================================================

UCruxCharacterMovementComponent::UCruxCharacterMovementComponent()
{
    // Tell UE we want our Tick to run
    PrimaryComponentTick.bCanEverTick = true;

    // Raise default air control so wall-jump steering feels responsive
    AirControl = 0.6f;

    // Standard jump height — tune with your scale
    JumpZVelocity = 620.f;

    // Slightly higher gravity than default to make movement feel snappy, not floaty
    GravityScale = 1.4f;
}

// ============================================================================
//  TICK — timers and per-frame checks outside of physics modes
// ============================================================================

void UCruxCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Count down micro-charge cooldown
    if (MicroChargeCDTimer > 0.f)
        MicroChargeCDTimer = FMath::Max(0.f, MicroChargeCDTimer - DeltaTime);

    // Auto-attempt wall-run while airborne and moving fast enough
    // (Player doesn't press a button — just running into a wall triggers it)
    if (IsFalling() && !bIsWallRunning && Velocity.SizeSquared2D() > FMath::Square(WallRunMinSpeed))
        TryStartWallRun();

    // Auto-attempt mantle when close to a ledge while falling
    if (IsFalling() && !bIsMantling)
        TryMantle();
}

// ============================================================================
//  PhysCustom — THE HEART OF CUSTOM MOVEMENT
//  UE calls this every tick when MovementMode == MOVE_Custom.
//  Dispatch to the appropriate physics handler.
// ============================================================================

void UCruxCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
    if (DeltaTime < MIN_TICK_TIME) return;

    switch (CustomMovementMode)
    {
        case CMOVE_Slide:    PhysSlide(DeltaTime);    break;
        case CMOVE_WallRun:  PhysWallRun(DeltaTime);  break;
        case CMOVE_Wingsuit: PhysWingsuit(DeltaTime); break;
        case CMOVE_Mantle:   PhysMantle(DeltaTime);   break;
        default: break;
    }

    // Always call Super so UE can do root motion and replication bookkeeping
    Super::PhysCustom(DeltaTime, Iterations);
}

// ============================================================================
//  SLIDE PHYSICS
// ============================================================================

void UCruxCharacterMovementComponent::EnterSlide()
{
    // Must be sprinting and above minimum speed
    if (Velocity.Size2D() < SlideMinSpeed) return;

    bIsSliding = true;
    SetCustomMode(CMOVE_Slide);

    // Shrink capsule so the player fits under obstacles while sliding
    // The Character's Crouch() handles capsule resize — we call it here
    CharacterOwner->Crouch();
}

void UCruxCharacterMovementComponent::ExitSlide()
{
    bIsSliding = false;
    CharacterOwner->UnCrouch();
    SetMovementMode(MOVE_Walking);
}

void UCruxCharacterMovementComponent::PhysSlide(float DeltaTime)
{
    // ── 1. Get the slope normal so we can factor gravity into the slide ──────
    FHitResult FloorHit;
    const FVector TraceStart = UpdatedComponent->GetComponentLocation();
    const FVector TraceEnd   = TraceStart - FVector(0, 0, SlideFloorTraceLength);

    GetWorld()->LineTraceSingleByChannel(FloorHit, TraceStart, TraceEnd,
        ECC_WorldStatic);

    // ── 2. Apply slope-amplified gravity to Velocity ─────────────────────────
    //
    //  This is the core of "sliding down a slope feels fast":
    //  We decompose gravity along the floor plane. On flat ground the component
    //  is zero. On a steep slope it's large — pure physics, no magic numbers.
    //
    if (FloorHit.bBlockingHit)
    {
        const FVector GravityDir     = FVector(0, 0, -1);
        const FVector FloorNormal    = FloorHit.Normal;
        // Project gravity onto the slope surface → the "downhill" direction
        const FVector SlopeGravity   = GravityDir -
            (FVector::DotProduct(GravityDir, FloorNormal) * FloorNormal);

        // Scale by our gravity amplifier and add to velocity this frame
        Velocity += SlopeGravity * GetGravityZ() * SlideGravityScale * DeltaTime * -1.f;
    }

    // ── 3. Apply sliding friction (much lower than walking friction) ──────────
    //
    //  UE's CalcVelocity would apply full walking friction and kill the slide.
    //  We override CalcVelocity() below to skip that. Here we apply our own
    //  reduced friction manually: Velocity decays by (1 - friction) each second.
    //
    const float FrictionFactor = FMath::Max(0.f, 1.f - SlideFriction * DeltaTime);
    Velocity *= FrictionFactor;

    // ── 4. Actually move the capsule ─────────────────────────────────────────
    //  MoveUpdatedComponent is the UE function that physically moves the actor
    //  and handles collision. Delta = how far to move this frame.
    //
    FVector Delta = Velocity * DeltaTime;
    FHitResult MoveHit;
    SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, MoveHit);

    // If we hit a wall mid-slide, slide along it (don't stop dead)
    if (MoveHit.IsValidBlockingHit())
        HandleImpact(MoveHit, DeltaTime, Delta);

    // ── 5. Exit conditions ────────────────────────────────────────────────────
    if (Velocity.Size2D() < SlideMinSpeed * 0.4f)
        ExitSlide();
}

void UCruxCharacterMovementComponent::SlideHop()
{
    if (!bIsSliding) return;

    // Preserve horizontal momentum — don't zero it out like a normal jump would
    // Just add the upward component. Horizontal Velocity is untouched.
    FVector HopVelocity = Velocity;
    HopVelocity.Z = JumpZVelocity;

    // Add a forward boost in the direction we're already moving
    const FVector HorizontalDir = Velocity.GetSafeNormal2D();
    HopVelocity += HorizontalDir * SlideHopImpulse;

    // LaunchCharacter: sets Velocity directly, ignoring current Z if bXYOverride=false
    CharacterOwner->LaunchCharacter(HopVelocity, true, true);

    ExitSlide(); // We're airborne now
}

// Override CalcVelocity so UE doesn't apply walking friction during our slide
void UCruxCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction,
    bool bFluid, float BrakingDeceleration)
{
    if (bIsSliding)
    {
        // Skip entirely — PhysSlide manages velocity directly
        return;
    }
    Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

// ============================================================================
//  WALL-RUN PHYSICS
// ============================================================================

bool UCruxCharacterMovementComponent::TryStartWallRun()
{
    const FVector ActorLoc = UpdatedComponent->GetComponentLocation();
    const FVector RightDir = UpdatedComponent->GetRightVector();

    // Trace left and right looking for a vertical surface
    for (const FVector& Dir : { RightDir, -RightDir })
    {
        FHitResult Hit;
        const FVector TraceEnd = ActorLoc + Dir * WallRunTraceDistance;

        GetWorld()->LineTraceSingleByChannel(Hit, ActorLoc, TraceEnd, ECC_WorldStatic);

#if WITH_EDITOR
        DrawDebugLine(GetWorld(), ActorLoc, TraceEnd,
            Hit.bBlockingHit ? FColor::Green : FColor::Red, false, 0.1f);
#endif

        if (Hit.bBlockingHit)
        {
            // Make sure the surface is roughly vertical (not a floor or ceiling)
            const float WallDot = FVector::DotProduct(Hit.Normal, FVector::UpVector);
            if (FMath::Abs(WallDot) < WallRunMaxFloorAngleDot) // Within ~17° of vertical
            {
                WallRunNormal  = Hit.Normal;
                bIsWallRunning = true;
                SetCustomMode(CMOVE_WallRun);
                return true;
            }
        }
    }
    return false;
}

void UCruxCharacterMovementComponent::WallJump()
{
    if (!bIsWallRunning) return;

    // Launch away from the wall + upward
    // SurfaceFrictionOverride affects how "explosive" the jump feels:
    // low friction (glass) = less grip so less wall-jump height
    const FVector JumpDir = WallRunNormal + FVector(0, 0, 0.6f);
    const float   JumpStr = WallJumpOffForce * SurfaceFrictionOverride;

    CharacterOwner->LaunchCharacter(JumpDir.GetSafeNormal() * JumpStr, true, true);

    bIsWallRunning = false;
    SetMovementMode(MOVE_Falling);
}

void UCruxCharacterMovementComponent::PhysWallRun(float DeltaTime)
{
    if (ShouldExitWallRun())
    {
        bIsWallRunning = false;
        SetMovementMode(MOVE_Falling);
        return;
    }

    // ── Project velocity onto the wall plane ───────────────────────────────
    //
    //  WallRunNormal points away from the wall. Projecting Velocity onto the
    //  plane perpendicular to that normal strips out any "into the wall" component,
    //  keeping movement parallel to the surface. This is the core of wall-running:
    //  the character wants to go forward, gravity wants to pull them down,
    //  and we kill both the "through the wall" and most of the "falling" component.
    //
    Velocity = FVector::VectorPlaneProject(Velocity, WallRunNormal);

    // Apply very low gravity (we're sticking to a wall, not free-falling)
    Velocity.Z -= GetGravityZ() * WallRunGravityScale * DeltaTime * -1.f;

    // Surface friction affects speed — rough wall = more drag
    const float WallDrag = FMath::Lerp(0.02f, 0.08f, SurfaceFrictionOverride - 0.5f);
    Velocity *= FMath::Max(0.f, 1.f - WallDrag * DeltaTime);

    FHitResult MoveHit;
    SafeMoveUpdatedComponent(Velocity * DeltaTime,
        UpdatedComponent->GetComponentQuat(), true, MoveHit);
}

bool UCruxCharacterMovementComponent::ShouldExitWallRun() const
{
    // Exit if horizontal speed dropped too low
    if (Velocity.Size2D() < WallRunMinSpeed * 0.5f) return true;

    // Re-trace to confirm the wall is still there
    const FVector ActorLoc  = UpdatedComponent->GetComponentLocation();
    const FVector WallDir   = -WallRunNormal; // Toward the wall
    FHitResult Hit;

    GetWorld()->LineTraceSingleByChannel(Hit, ActorLoc,
        ActorLoc + WallDir * WallRunTraceDistance, ECC_WorldStatic);

    return !Hit.bBlockingHit; // Exit if wall disappeared
}

// ============================================================================
//  LEDGE MANTLE
// ============================================================================

bool UCruxCharacterMovementComponent::TryMantle()
{
    const FVector ActorLoc   = UpdatedComponent->GetComponentLocation();
    const FVector ForwardDir = UpdatedComponent->GetForwardVector();

    // ── Step 1: Is there a wall in front? ────────────────────────────────────
    FHitResult WallHit;
    GetWorld()->LineTraceSingleByChannel(WallHit,
        ActorLoc,
        ActorLoc + ForwardDir * MantleReachDistance,
        ECC_WorldStatic);

    if (!WallHit.bBlockingHit) return false;

    // ── Step 2: Is there a clear ledge top above the wall hit? ───────────────
    const FVector LedgeCheckStart = WallHit.ImpactPoint +
        FVector(0, 0, MantleMaxHeight) +
        ForwardDir * 10.f; // Slightly over the edge

    FHitResult LedgeHit;
    GetWorld()->LineTraceSingleByChannel(LedgeHit,
        LedgeCheckStart,
        LedgeCheckStart - FVector(0, 0, MantleMaxHeight + 20.f),
        ECC_WorldStatic);

    if (!LedgeHit.bBlockingHit) return false;

    // ── Step 3: Is there room for the capsule on the ledge? ──────────────────
    const float CapsuleHalfHeight =
        CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector LedgeSurface = LedgeHit.ImpactPoint + FVector(0, 0, CapsuleHalfHeight);

    // Simple overlap check — if blocked, there's a ceiling and we can't stand there
    TArray<FOverlapResult> Overlaps;
    const bool bClearSpace = !GetWorld()->OverlapAnyTestByChannel(
        LedgeSurface,
        FQuat::Identity,
        ECC_Pawn,
        FCollisionShape::MakeSphere(CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius())
    );

    if (!bClearSpace) return false;

    // ── Begin mantle ─────────────────────────────────────────────────────────
    MantleTarget = LedgeSurface;
    bIsMantling  = true;
    SetCustomMode(CMOVE_Mantle);
    // Note: We handle the lerp in PhysMantle through PhysCustom dispatch.

    return true;
}

void UCruxCharacterMovementComponent::PhysMantle(float DeltaTime)
{
    const FVector Current = UpdatedComponent->GetComponentLocation();
    const FVector NewPos  = FMath::VInterpTo(Current, MantleTarget, DeltaTime, MantleSpeed * 0.01f);
    const FVector Delta   = NewPos - Current;

    FHitResult Hit;
    SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

    // Arrived at target — exit mantle
    if (FVector::DistSquared(NewPos, MantleTarget) < FMath::Square(5.f))
    {
        bIsMantling = false;
        Velocity    = FVector::ZeroVector;
        SetMovementMode(MOVE_Walking);
    }
}

// ============================================================================
//  MICRO-CHARGE
// ============================================================================

void UCruxCharacterMovementComponent::FireMicroCharge(bool bHeld)
{
    if (MicroChargeCDTimer > 0.f) return; // Still on cooldown

    const FVector ChargeDir = UpdatedComponent->GetForwardVector();
    const float   Impulse   = bHeld ? MicroChargeHeldImpulse : MicroChargeTapImpulse;

    // ── This is the cleanest example of the momentum → movement link ─────────
    //
    //  LaunchCharacter() writes directly to Velocity.
    //  bXYOverride=false means we ADD to existing horizontal velocity rather than
    //  replacing it — so momentum is preserved and amplified, not reset.
    //  bZOverride=false means we don't kill vertical momentum either.
    //
    //  Result: character retains all current speed and gets a burst on top.
    //  This is what makes chaining feel fluid rather than stuttery.
    //
    const FVector ChargeImpulse = ChargeDir * Impulse;
    CharacterOwner->LaunchCharacter(ChargeImpulse, false, false);

    // For a held charge, also lock the trajectory (prevent steering mid-charge)
    // We do this by briefly zeroing AirControl
    if (bHeld)
    {
        AirControlBeforeMicroCharge = AirControl;
        AirControl = 0.f;

        // Re-enable after a short delay through a tracked, UObject-safe timer.
        GetWorld()->GetTimerManager().SetTimer(
            MicroChargeTimerHandle,
            this,
            &UCruxCharacterMovementComponent::ResetAirControl,
            MicroChargeAirControlLockDuration,
            false
        );
    }

    MicroChargeCDTimer = MicroChargeCooldown;
}

// ============================================================================
//  WINGSUIT
// ============================================================================

void UCruxCharacterMovementComponent::ToggleWingsuit()
{
    bWingsuitActive = !bWingsuitActive;

    if (bWingsuitActive && IsFalling())
        SetCustomMode(CMOVE_Wingsuit);
    else if (!bWingsuitActive)
        SetMovementMode(MOVE_Falling); // Hand back to UE gravity
}

void UCruxCharacterMovementComponent::PhysWingsuit(float DeltaTime)
{
    // ── Dive converts to horizontal speed ────────────────────────────────────
    //  If the player is aimed downward (negative Z velocity), convert some of
    //  that falling speed into forward speed — the core of wingsuit physics.
    if (Velocity.Z < 0.f)
    {
        const float DiveSpeed    = FMath::Abs(Velocity.Z);
        const FVector ForwardDir = UpdatedComponent->GetForwardVector().GetSafeNormal2D();

        const float ClampedDiveTransferRate = FMath::Clamp(WingsuitDiveTransferRate, 0.f, 1.f);
        Velocity   += ForwardDir * DiveSpeed * ClampedDiveTransferRate;
        Velocity.Z *= 1.f - ClampedDiveTransferRate;
    }

    // Apply very low gravity
    Velocity.Z -= GetGravityZ() * WingsuitGravityScale * DeltaTime * -1.f;

    // Horizontal drag cap — prevents runaway glide acceleration
    FVector HorizontalVel = FVector(Velocity.X, Velocity.Y, 0.f);
    if (HorizontalVel.Size() > WingsuitMaxGlideSpeed)
    {
        HorizontalVel  = HorizontalVel.GetSafeNormal() * WingsuitMaxGlideSpeed;
        Velocity.X     = HorizontalVel.X;
        Velocity.Y     = HorizontalVel.Y;
    }

    // Apply general drag
    Velocity *= FMath::Max(0.f, 1.f - WingsuitDrag * DeltaTime);

    FHitResult Hit;
    SafeMoveUpdatedComponent(Velocity * DeltaTime,
        UpdatedComponent->GetComponentQuat(), true, Hit);

    // Land detection — hand back to walking if we hit ground
    if (Hit.bBlockingHit && Hit.Normal.Z > 0.7f)
    {
        bWingsuitActive = false;
        SetMovementMode(MOVE_Walking);
    }
}

// ============================================================================
//  DASH RESET — fires on specific landings
// ============================================================================

void UCruxCharacterMovementComponent::ProcessLanded(const FHitResult& Hit,
    float remainingTime, int32 Iterations)
{
    Super::ProcessLanded(Hit, remainingTime, Iterations);

    // Reset dash availability on any landing
    // In the full implementation, check if we landed on a "bounce object" physics tag
    // to conditionally reset vs. always reset
    bDashAvailable = true;
    MicroChargeCDTimer = 0.f; // Also resets micro-charge on ground contact
}

// ============================================================================
//  SURFACE FRICTION — ML PIPELINE HOOK
// ============================================================================

void UCruxCharacterMovementComponent::SetSurfaceFriction(float InFriction)
{
    // This is the seam between your ML pipeline and the physics system.
    // When ML classifies a surface as "glass" it calls SetSurfaceFriction(0.5f).
    // When "concrete" it calls SetSurfaceFriction(1.0f). The physics above read
    // SurfaceFrictionOverride and apply it — no other changes needed.
    SurfaceFrictionOverride = FMath::Clamp(InFriction, 0.1f, 2.0f);
}

// ============================================================================
//  UTILITY
// ============================================================================

void UCruxCharacterMovementComponent::SetCustomMode(ECustomMovementMode NewMode)
{
    SetMovementMode(MOVE_Custom, NewMode);
}

void UCruxCharacterMovementComponent::OnMovementModeChanged(
    EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
    Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

    if (GetWorld() && GetWorld()->GetTimerManager().IsTimerActive(MicroChargeTimerHandle))
    {
        GetWorld()->GetTimerManager().ClearTimer(MicroChargeTimerHandle);
        ResetAirControl();
    }

    // Clean up state flags when leaving custom modes
    if (PreviousMovementMode == MOVE_Custom)
    {
        if (PreviousCustomMode == CMOVE_Slide)    bIsSliding      = false;
        if (PreviousCustomMode == CMOVE_WallRun)  bIsWallRunning  = false;
        if (PreviousCustomMode == CMOVE_Wingsuit) bWingsuitActive = false;
        if (PreviousCustomMode == CMOVE_Mantle)   bIsMantling     = false;
    }
}

void UCruxCharacterMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(MicroChargeTimerHandle);
    }

    Super::EndPlay(EndPlayReason);
}

float UCruxCharacterMovementComponent::GetMaxSpeed() const
{
    if (bIsSliding)     return MaxSlideSpeed;
    if (bIsWallRunning) return MaxWallRunSpeed;
    if (bWingsuitActive) return WingsuitMaxGlideSpeed;
    return Super::GetMaxSpeed();
}

void UCruxCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);

    bIsSliding      = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
    bIsWallRunning  = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
    bWingsuitActive = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
    bIsMantling     = (Flags & FSavedMove_Character::FLAG_Custom_3) != 0;
}

FNetworkPredictionData_Client* UCruxCharacterMovementComponent::GetPredictionData_Client() const
{
    check(PawnOwner != nullptr);

    if (ClientPredictionData == nullptr)
    {
        UCruxCharacterMovementComponent* MutableThis = const_cast<UCruxCharacterMovementComponent*>(this);
        MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Crux(*this);
    }

    return ClientPredictionData;
}

void UCruxCharacterMovementComponent::ResetAirControl()
{
    AirControl = AirControlBeforeMicroCharge;
}

FSavedMove_Crux::FSavedMove_Crux()
{
    Clear();
}

void FSavedMove_Crux::Clear()
{
    Super::Clear();

    bSavedIsSliding = false;
    bSavedIsWallRunning = false;
    bSavedWingsuitActive = false;
    bSavedIsMantling = false;
    SavedCustomMovementMode = CMOVE_MAX;
}

uint8 FSavedMove_Crux::GetCompressedFlags() const
{
    uint8 Result = Super::GetCompressedFlags();

    if (bSavedIsSliding)      Result |= FLAG_Custom_0;
    if (bSavedIsWallRunning)  Result |= FLAG_Custom_1;
    if (bSavedWingsuitActive) Result |= FLAG_Custom_2;
    if (bSavedIsMantling)     Result |= FLAG_Custom_3;

    return Result;
}

bool FSavedMove_Crux::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
    const FSavedMove_Crux* NewCruxMove = static_cast<const FSavedMove_Crux*>(NewMove.Get());
    if (bSavedIsSliding != NewCruxMove->bSavedIsSliding ||
        bSavedIsWallRunning != NewCruxMove->bSavedIsWallRunning ||
        bSavedWingsuitActive != NewCruxMove->bSavedWingsuitActive ||
        bSavedIsMantling != NewCruxMove->bSavedIsMantling ||
        SavedCustomMovementMode != NewCruxMove->SavedCustomMovementMode)
    {
        return false;
    }

    return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Crux::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
    Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

    if (const UCruxCharacterMovementComponent* CruxMove = Cast<UCruxCharacterMovementComponent>(Character->GetCharacterMovement()))
    {
        bSavedIsSliding = CruxMove->bIsSliding;
        bSavedIsWallRunning = CruxMove->bIsWallRunning;
        bSavedWingsuitActive = CruxMove->bWingsuitActive;
        bSavedIsMantling = CruxMove->bIsMantling;
        SavedCustomMovementMode = CruxMove->CustomMovementMode;
    }
}

void FSavedMove_Crux::PrepMoveFor(ACharacter* Character)
{
    Super::PrepMoveFor(Character);

    if (UCruxCharacterMovementComponent* CruxMove = Cast<UCruxCharacterMovementComponent>(Character->GetCharacterMovement()))
    {
        CruxMove->bIsSliding = bSavedIsSliding;
        CruxMove->bIsWallRunning = bSavedIsWallRunning;
        CruxMove->bWingsuitActive = bSavedWingsuitActive;
        CruxMove->bIsMantling = bSavedIsMantling;
    }
}

FNetworkPredictionData_Client_Crux::FNetworkPredictionData_Client_Crux(const UCharacterMovementComponent& ClientMovement)
    : Super(ClientMovement)
{
}

FSavedMovePtr FNetworkPredictionData_Client_Crux::AllocateNewMove()
{
    return FSavedMovePtr(new FSavedMove_Crux());
}
