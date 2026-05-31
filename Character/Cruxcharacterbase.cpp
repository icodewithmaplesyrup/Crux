#include "CruxCharacterBase.h"
#include "CruxHotbarComponent.h"
#include "CruxCharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/Controller.h"
#include "HAL/PlatformTime.h"

// ============================================================================
//  CONSTRUCTION
//  Pass a custom ObjectInitializer that swaps out UE's default CMC for ours.
//  This is the standard UE5 pattern for replacing the movement component.
// ============================================================================

ACruxCharacterBase::ACruxCharacterBase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UCruxCharacterMovementComponent>(
        ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;

    // Hotbar — weapon classes are assigned in Blueprint via the UPROPERTY slots
    HotbarComponent = CreateDefaultSubobject<UCruxHotbarComponent>(TEXT("HotbarComponent"));
}

void ACruxCharacterBase::BeginPlay()
{
    Super::BeginPlay();
}

void ACruxCharacterBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // ── Right-click hold detection ─────────────────────────────────────────────
    // If the player is holding RMB and hasn't released yet, we track time here.
    // The actual SecondaryFire call happens on release (OnSecondaryFireReleased).
    // Nothing additional needed in Tick for this — the press time is recorded
    // in OnSecondaryFirePressed and evaluated in OnSecondaryFireReleased.
}

// ============================================================================
//  ACCESSORS
// ============================================================================

UCruxCharacterMovementComponent* ACruxCharacterBase::GetCruxMovement() const
{
    return Cast<UCruxCharacterMovementComponent>(GetCharacterMovement());
}

// ============================================================================
//  INPUT SETUP
// ============================================================================
//
//  Bind each input action to a member function. The action names here must
//  match what's defined in Project Settings → Input → Action Mappings.
//
//  Suggested mapping table (configure in UE editor):
//    Action "MoveForward"      → W/S (axis)
//    Action "MoveRight"        → A/D (axis)
//    Action "LookUp"           → Mouse Y (axis)
//    Action "Turn"             → Mouse X (axis)
//    Action "Jump"             → Space
//    Action "Crouch"           → Left Ctrl
//    Action "Wingsuit"         → F
//    Action "PrimaryFire"      → Left Mouse Button
//    Action "SecondaryFire"    → Right Mouse Button
//    Action "Slot1"            → Middle Mouse Button
//    Action "Slot2"            → Mouse Scroll Up
//    Action "Slot3"            → Mouse Scroll Down
//
// ============================================================================

void ACruxCharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    check(PlayerInputComponent);

    // ── Axis bindings ─────────────────────────────────────────────────────────

    PlayerInputComponent->BindAxis("MoveForward", this, &ACruxCharacterBase::MoveForward);
    PlayerInputComponent->BindAxis("MoveRight",   this, &ACruxCharacterBase::MoveRight);
    PlayerInputComponent->BindAxis("LookUp",      this, &ACruxCharacterBase::LookUp);
    PlayerInputComponent->BindAxis("Turn",        this, &ACruxCharacterBase::Turn);

    // ── Action bindings — movement ────────────────────────────────────────────

    PlayerInputComponent->BindAction("Jump",     IE_Pressed,  this, &ACruxCharacterBase::OnJumpPressed);
    PlayerInputComponent->BindAction("Jump",     IE_Released, this, &ACruxCharacterBase::OnJumpReleased);
    PlayerInputComponent->BindAction("Crouch",   IE_Pressed,  this, &ACruxCharacterBase::OnCrouchPressed);
    PlayerInputComponent->BindAction("Crouch",   IE_Released, this, &ACruxCharacterBase::OnCrouchReleased);
    PlayerInputComponent->BindAction("Wingsuit", IE_Pressed,  this, &ACruxCharacterBase::OnToggleWingsuit);

    // ── Action bindings — weapon fire ─────────────────────────────────────────

    PlayerInputComponent->BindAction("PrimaryFire",   IE_Pressed,  this, &ACruxCharacterBase::OnPrimaryFirePressed);
    PlayerInputComponent->BindAction("SecondaryFire", IE_Pressed,  this, &ACruxCharacterBase::OnSecondaryFirePressed);
    PlayerInputComponent->BindAction("SecondaryFire", IE_Released, this, &ACruxCharacterBase::OnSecondaryFireReleased);

    // ── Action bindings — hotbar ──────────────────────────────────────────────

    PlayerInputComponent->BindAction("Slot1", IE_Pressed, this, &ACruxCharacterBase::OnSlot1);
    PlayerInputComponent->BindAction("Slot2", IE_Pressed, this, &ACruxCharacterBase::OnSlot2);
    PlayerInputComponent->BindAction("Slot3", IE_Pressed, this, &ACruxCharacterBase::OnSlot3);
}

// ============================================================================
//  MOVEMENT INPUT
// ============================================================================

void ACruxCharacterBase::MoveForward(float Value)
{
    if (Controller && Value != 0.f)
    {
        // Move along the controller's forward vector (ignores pitch — stays horizontal)
        const FRotator Rotation = Controller->GetControlRotation();
        const FRotator YawOnly  = FRotator(0.f, Rotation.Yaw, 0.f);
        const FVector  Dir      = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
        AddMovementInput(Dir, Value);
    }
}

void ACruxCharacterBase::MoveRight(float Value)
{
    if (Controller && Value != 0.f)
    {
        const FRotator Rotation = Controller->GetControlRotation();
        const FRotator YawOnly  = FRotator(0.f, Rotation.Yaw, 0.f);
        const FVector  Dir      = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);
        AddMovementInput(Dir, Value);
    }
}

void ACruxCharacterBase::LookUp(float Value)
{
    AddControllerPitchInput(Value);
}

void ACruxCharacterBase::Turn(float Value)
{
    AddControllerYawInput(Value);
}

// ============================================================================
//  JUMP / SLIDE-HOP / WALL JUMP
// ============================================================================

void ACruxCharacterBase::OnJumpPressed()
{
    UCruxCharacterMovementComponent* CruxMovement = GetCruxMovement();
    if (!CruxMovement) return;

    // Priority order:
    //   1. Sliding → SlideHop (converts momentum into airborne boost)
    //   2. Wall-running → WallJump
    //   3. Default → ACharacter::Jump()
    if (CruxMovement->bIsSliding)
    {
        CruxMovement->SlideHop();
    }
    else if (CruxMovement->bIsWallRunning)
    {
        CruxMovement->WallJump();
    }
    else
    {
        Jump();
    }
}

void ACruxCharacterBase::OnJumpReleased()
{
    StopJumping();
}

void ACruxCharacterBase::OnSlideHop()
{
    // Kept as a separate binding point in case a dedicated slide-hop key is wanted.
    UCruxCharacterMovementComponent* CruxMovement = GetCruxMovement();
    if (CruxMovement) CruxMovement->SlideHop();
}

// ============================================================================
//  CROUCH / SLIDE
// ============================================================================

void ACruxCharacterBase::OnCrouchPressed()
{
    UCruxCharacterMovementComponent* CruxMovement = GetCruxMovement();
    if (!CruxMovement) return;

    // Crouch while moving fast → slide
    if (CruxMovement->Velocity.SizeSquared2D() > FMath::Square(CruxMovement->SlideMinSpeed))
        CruxMovement->EnterSlide();
    else
        Crouch();
}

void ACruxCharacterBase::OnCrouchReleased()
{
    UCruxCharacterMovementComponent* CruxMovement = GetCruxMovement();
    if (!CruxMovement) return;

    if (CruxMovement->bIsSliding)
        CruxMovement->ExitSlide();
    else
        UnCrouch();
}

// ============================================================================
//  WINGSUIT
// ============================================================================

void ACruxCharacterBase::OnToggleWingsuit()
{
    UCruxCharacterMovementComponent* CruxMovement = GetCruxMovement();
    if (CruxMovement) CruxMovement->ToggleWingsuit();
}

// ============================================================================
//  WALL JUMP
// ============================================================================

void ACruxCharacterBase::OnWallJump()
{
    UCruxCharacterMovementComponent* CruxMovement = GetCruxMovement();
    if (CruxMovement) CruxMovement->WallJump();
}

// ============================================================================
//  HOTBAR SLOT SELECTION
// ============================================================================

void ACruxCharacterBase::OnSlot1()
{
    if (HotbarComponent) HotbarComponent->EquipSlot1();
}

void ACruxCharacterBase::OnSlot2()
{
    if (HotbarComponent) HotbarComponent->EquipSlot2();
}

void ACruxCharacterBase::OnSlot3()
{
    if (HotbarComponent) HotbarComponent->EquipSlot3();
}

// ============================================================================
//  WEAPON FIRE
// ============================================================================

void ACruxCharacterBase::OnPrimaryFirePressed()
{
    if (HotbarComponent) HotbarComponent->PrimaryFire();
}

// ── Right-click hold detection ────────────────────────────────────────────────
//
//  UE's input system doesn't natively distinguish tap vs. hold with a single
//  action binding. We implement it manually:
//
//    Press   → record the time
//    Release → compute duration
//              duration < SecondaryHoldThreshold → SecondaryFire(false)  [tap]
//              duration >= SecondaryHoldThreshold → SecondaryFire(true)  [held]
//
//  This mirrors the CMC's MicroChargeHoldThreshold logic — both are set to 0.3s
//  by default and should stay in sync. The character-side threshold is the
//  "input gate"; the CMC threshold is the "physics gate". They should match.
//
// ─────────────────────────────────────────────────────────────────────────────

void ACruxCharacterBase::OnSecondaryFirePressed()
{
    bSecondaryHeld     = true;
    SecondaryPressTime = static_cast<float>(FPlatformTime::Seconds());
}

void ACruxCharacterBase::OnSecondaryFireReleased()
{
    if (!bSecondaryHeld) return;
    bSecondaryHeld = false;

    const float HoldDuration = static_cast<float>(FPlatformTime::Seconds()) - SecondaryPressTime;
    const bool  bWasHeld     = HoldDuration >= SecondaryHoldThreshold;

    if (HotbarComponent) HotbarComponent->SecondaryFire(bWasHeld);
}
