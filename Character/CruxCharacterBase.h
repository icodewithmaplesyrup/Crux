#pragma once

#include "CoreMinimal.h"
#include "../CruxCompatibility.h"
#include "GameFramework/Character.h"
#include "CruxCharacterBase.generated.h"

class UCruxCharacterMovementComponent;
class UCruxHotbarComponent;
class UInputComponent;

/**
 * Minimal Crux player character that is safe to add to a blank Unreal project.
 *
 * The original implementation was written around an FPS template character. This
 * class now owns only standard engine dependencies, a custom movement component,
 * and the Crux hotbar, so it can be used as the Default Pawn Class in an empty
 * C++ project or subclassed from Blueprint.
 */
UCLASS(BlueprintType, Blueprintable)
class CRUX_API ACruxCharacterBase : public ACharacter
{
    GENERATED_BODY()

public:
    ACruxCharacterBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    UFUNCTION(BlueprintPure, Category = "Crux|Movement")
    UCruxCharacterMovementComponent* GetCruxMovement() const;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crux|Weapons")
    UCruxHotbarComponent* HotbarComponent = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crux|Input")
    float SecondaryHoldThreshold = 0.3f;

    void MoveForward(float Value);
    void MoveRight(float Value);
    void LookUp(float Value);
    void Turn(float Value);

    void OnJumpPressed();
    void OnJumpReleased();
    void OnSlideHop();
    void OnCrouchPressed();
    void OnCrouchReleased();
    void OnToggleWingsuit();
    void OnWallJump();

    void OnSlot1();
    void OnSlot2();
    void OnSlot3();

    void OnPrimaryFirePressed();
    void OnSecondaryFirePressed();
    void OnSecondaryFireReleased();

private:
    bool bSecondaryHeld = false;
    float SecondaryPressTime = 0.f;
};
