#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CruxWeaponBase.generated.h"

class UCruxCharacterMovementComponent;
class ACharacter;

// ============================================================================
//  HOTBAR SLOT — maps to the control scheme in the GDD
//  Slot1 (M3)   = Launchers / Verticality  (Mace)
//  Slot2 (Up)   = Bridgers / Horizontal    (Sword, Spear)
//  Slot3 (Down) = Finishers / Ranged       (Bow, Crossbow)
// ============================================================================
UENUM(BlueprintType)
enum class ECruxWeaponSlot : uint8
{
    Slot1_Verticality   UMETA(DisplayName = "Slot 1 — Verticality"),
    Slot2_Horizontal    UMETA(DisplayName = "Slot 2 — Horizontal"),
    Slot3_Ranged        UMETA(DisplayName = "Slot 3 — Ranged"),
};

// ============================================================================
//  FLOW HOOK CONTEXT
//  Passed to TriggerFlowHook() so each weapon knows exactly what it hit
//  and can choose the right momentum response.
// ============================================================================
USTRUCT(BlueprintType)
struct FCruxFlowHookContext
{
    GENERATED_BODY()

    // The actor we struck (nullptr on a geometry/wall hit)
    UPROPERTY() AActor* HitActor  = nullptr;

    // World-space surface normal at the impact point
    UPROPERTY() FVector HitNormal = FVector::ZeroVector;

    // Impact point in world space
    UPROPERTY() FVector ImpactPoint = FVector::ZeroVector;

    // True when the Micro-Charge was still active at the moment of impact
    // (i.e. this was a lunge hit, not a standing primary fire hit)
    UPROPERTY() bool bWasMicroChargeActive = false;

    // True when this hit is geometry (wall/floor), not a character
    UPROPERTY() bool bIsGeometryHit = false;
};

// ============================================================================
//  ACruxWeaponBase
//
//  Every weapon inherits from this. The three things every weapon does:
//    1. PrimaryFire()          — left-click, always fast and button-mash viable
//    2. StartMicroCharge() /
//       ReleaseMicroCharge()   — right-click tap vs. held (0.3 s threshold)
//    3. TriggerFlowHook()      — fires when the weapon's flow condition is met
//
//  The weapon does NOT own input — the Character/PlayerController calls these.
//  The weapon owns the PHYSICS RESPONSE.
// ============================================================================
UCLASS(Abstract, BlueprintType, Blueprintable)
class CRUX_API ACruxWeaponBase : public AActor
{
    GENERATED_BODY()

public:
    ACruxWeaponBase();

    // ── Lifecycle ────────────────────────────────────────────────────────────

    // Called by the weapon manager when this weapon becomes the active slot
    virtual void OnEquip(ACharacter* NewOwner);

    // Called when swapping away — clean up any in-progress charge state
    virtual void OnUnequip();

    // ── Core weapon actions (called by the owning Character's input handler) ─

    // Left-click: fast, reliable, button-mash viable
    UFUNCTION(BlueprintCallable, Category = "Crux|Weapon")
    virtual void PrimaryFire() {}

    // Right-click pressed — starts timing the hold
    UFUNCTION(BlueprintCallable, Category = "Crux|Weapon")
    virtual void StartMicroCharge();

    // Right-click released — fires tap or held charge based on hold duration
    // MicroChargeHeldThreshold (0.3s from GDD) distinguishes tap from hold
    UFUNCTION(BlueprintCallable, Category = "Crux|Weapon")
    virtual void ReleaseMicroCharge();

    // ── Flow Hook ─────────────────────────────────────────────────────────────
    //
    //  Each weapon has a unique "Flow Hook" — a conditional momentum bonus that
    //  triggers when a specific event occurs (hitting an enemy, hitting a wall,
    //  landing a stomp, etc.).  The Character calls this when a hit is detected;
    //  the weapon decides whether its flow condition is met.
    //
    UFUNCTION(BlueprintCallable, Category = "Crux|Weapon")
    virtual void TriggerFlowHook(const FCruxFlowHookContext& Context) {}

    // ── Accessors ─────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintPure, Category = "Crux|Weapon")
    ECruxWeaponSlot GetHotbarSlot() const { return HotbarSlot; }

    UFUNCTION(BlueprintPure, Category = "Crux|Weapon")
    bool IsCharging() const { return bIsCharging; }

    UFUNCTION(BlueprintPure, Category = "Crux|Weapon")
    float GetChargeHeldTime() const;

protected:
    // ── Config ────────────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, Category = "Crux|Weapon|Config")
    ECruxWeaponSlot HotbarSlot = ECruxWeaponSlot::Slot2_Horizontal;

    // Seconds of hold that distinguishes a tap from a held micro-charge (GDD: 0.3s)
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Weapon|Config")
    float MicroChargeHeldThreshold = 0.3f;

    // Short trace range for melee weapons
    UPROPERTY(EditDefaultsOnly, Category = "Crux|Weapon|Config")
    float MeleeTraceRange = 150.f;

    // ── Runtime state ─────────────────────────────────────────────────────────

    // The character that currently holds this weapon
    UPROPERTY()
    ACharacter* OwnerCharacter = nullptr;

    // Convenience pointer — cached on equip
    UPROPERTY()
    UCruxCharacterMovementComponent* CruxMovement = nullptr;

    bool  bIsCharging = false;
    float ChargeStartTime = 0.f;   // FPlatformTime::Seconds() at charge start

    // ── Helpers ───────────────────────────────────────────────────────────────

    // Performs a sphere-sweep forward from the character's camera/mesh and
    // returns the first blocking hit. Used by melee primary fire and the
    // Micro-Charge lunge impact check.
    bool DoMeleeTrace(FHitResult& OutHit, float Range, float SphereRadius = 30.f) const;

    // Quick reach to the movement component's LaunchCharacter wrapper.
    // bOverrideXY = false preserves existing horizontal momentum (additive).
    void ApplyImpulse(FVector Impulse, bool bOverrideXY = false, bool bOverrideZ = false) const;
};
