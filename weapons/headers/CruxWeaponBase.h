#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "CruxWeaponBase.generated.h"

class UCruxCharacterMovementComponent;
class ACharacter;

// ============================================================================
//  HOTBAR SLOT — maps to the control scheme in the GDD
//  Slot1 (M3)   = Launchers / Verticality  (Mace)
//  Slot2 (Up)   = Bridgers / Horizontal    (Sword, Spear)
//  Slot3 (Down) = Finishers / Ranged       (Bow, Crossbow)
//
//  NOTE: This is the canonical enum. All weapon subclasses and the hotbar
//  component use EWeaponSlot. The old ECruxWeaponSlot name is retired.
// ============================================================================
UENUM(BlueprintType)
enum class EWeaponSlot : uint8
{
    Slot1_Launcher  UMETA(DisplayName = "Slot 1 — Launcher / Verticality"),
    Slot2_Bridger   UMETA(DisplayName = "Slot 2 — Bridger / Horizontal"),
    Slot3_Finisher  UMETA(DisplayName = "Slot 3 — Finisher / Ranged"),
};

// ============================================================================
//  FLOW HOOK HIT CLASSIFICATION
//  Returned by ClassifyHit() and passed into OnFlowHook() so each weapon
//  can branch on what it actually struck without re-examining the hit result.
// ============================================================================
UENUM(BlueprintType)
enum class EFlowHookTarget : uint8
{
    Miss    UMETA(DisplayName = "Miss — nothing hit"),
    Enemy   UMETA(DisplayName = "Enemy / Pawn"),
    Surface UMETA(DisplayName = "Geometry / Surface"),
};

// ============================================================================
//  ACruxWeaponBase
//
//  Every weapon inherits from this. The three things every weapon does:
//    1. PrimaryFire()     — left-click, always fast and button-mash viable
//    2. SecondaryFire()   — right-click; bHeld distinguishes tap from hold
//    3. OnFlowHook()      — fires when the weapon's unique flow condition is met
//
//  The weapon does NOT own input — the Character/HotbarComponent calls these.
//  The weapon owns the PHYSICS RESPONSE.
//
//  Subclass contract:
//    - Override PrimaryFire(), SecondaryFire(), OnFlowHook().
//    - Call Super:: at the start of each — the base does guard checks and
//      early-returns if !bIsEquipped or owner pointers are null.
//    - Set WeaponSlot, PrimaryDamage, SecondaryDamage, MeleeTraceLength,
//      MeleeTraceRadius, WeaponDisplayName in the subclass constructor.
// ============================================================================
UCLASS(Abstract, BlueprintType, Blueprintable)
class CRUX_API ACruxWeaponBase : public AActor
{
    GENERATED_BODY()

public:
    ACruxWeaponBase();

    // ── Lifecycle — called by UCruxHotbarComponent ────────────────────────────

    /** Make this weapon visible and usable. Caches owner pointers. */
    virtual void Equip(ACharacter* NewOwnerCharacter);

    /** Hide this weapon without destroying it. Pointers are kept for re-equip. */
    virtual void Holster();

    // ── Core weapon actions — called by the owning Character's input handler ──

    /** Left-click: fast, reliable, button-mash viable. */
    UFUNCTION(BlueprintCallable, Category = "Crux|Weapon")
    virtual void PrimaryFire();

    /**
     * Right-click action.
     * @param bHeld  true when the player held past CMC's MicroChargeHoldThreshold (0.3s).
     *               false = tap variant (quick burst), true = held variant (committed lunge).
     */
    UFUNCTION(BlueprintCallable, Category = "Crux|Weapon")
    virtual void SecondaryFire(bool bHeld);

    /**
     * Flow Hook callback — fired when this weapon's unique flow condition is met.
     * Each subclass overrides this to apply its momentum bonus.
     * @param HitResult  The blocking hit that triggered the flow condition (may be default).
     * @param Target     What we hit: Miss, Enemy, or Surface.
     */
    UFUNCTION(BlueprintCallable, Category = "Crux|Weapon")
    virtual void OnFlowHook(const FHitResult& HitResult, EFlowHookTarget Target);

    // ── Accessors ─────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintPure, Category = "Crux|Weapon")
    EWeaponSlot GetWeaponSlot() const { return WeaponSlot; }

    UFUNCTION(BlueprintPure, Category = "Crux|Weapon")
    bool GetIsEquipped() const { return bIsEquipped; }

    UFUNCTION(BlueprintPure, Category = "Crux|Weapon")
    FText GetWeaponDisplayName() const { return WeaponDisplayName; }

protected:
    // ── Identity / Config — set in each subclass constructor ─────────────────

    /** Which hotbar slot this weapon occupies. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crux|Weapon|Config")
    EWeaponSlot WeaponSlot = EWeaponSlot::Slot2_Bridger;

    /** Shown in the HUD weapon slot display. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crux|Weapon|Config")
    FText WeaponDisplayName;

    // ── Damage values — set in subclass constructors ──────────────────────────

    /** Base damage for PrimaryFire hits. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crux|Weapon|Damage")
    float PrimaryDamage = 20.f;

    /** Base damage for SecondaryFire / lunge hits. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crux|Weapon|Damage")
    float SecondaryDamage = 10.f;

    /** Damage applied by the Flow Hook action (stomp kills, etc.). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crux|Weapon|Damage")
    float FlowHookDamage = 0.f;

    // ── Melee trace config — set in subclass constructors ─────────────────────

    /**
     * Forward length of the melee sweep (Unreal Units).
     * 0 = ranged only (Bow).
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crux|Weapon|Melee")
    float MeleeTraceLength = 150.f;

    /**
     * Sphere radius for the melee sweep.
     * 0 = pure line trace (very precise, e.g. Spear primary).
     * >0 = sphere sweep (forgiving, e.g. Sword, Mace).
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crux|Weapon|Melee")
    float MeleeTraceRadius = 20.f;

    // ── Runtime state ─────────────────────────────────────────────────────────

    /** The character that currently holds this weapon. Cached on Equip(). */
    UPROPERTY()
    ACharacter* OwnerCharacter = nullptr;

    /** Convenience pointer to the CMC — cached on Equip(). */
    UPROPERTY()
    UCruxCharacterMovementComponent* CruxMovement = nullptr;

    /** The visible weapon mesh. Created in the base constructor. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crux|Weapon")
    USkeletalMeshComponent* WeaponMesh = nullptr;

    /** True between Equip() and Holster(). Guards all fire functions. */
    bool bIsEquipped = false;

    // ── Shared helpers ────────────────────────────────────────────────────────

    /**
     * Forward sphere-sweep from the character's eye location.
     * Uses MeleeTraceLength (or OverrideLen if > 0) and MeleeTraceRadius.
     * Falls back to a line trace when MeleeTraceRadius == 0.
     */
    bool MeleeSweep(FHitResult& OutHit, float OverrideLen = 0.f) const;

    /**
     * Classify a blocking hit result into Enemy, Surface, or Miss.
     * Used to decide which Flow Hook branch to execute.
     */
    EFlowHookTarget ClassifyHit(const FHitResult& Hit) const;

    /**
     * Apply point damage to an actor through the standard UE damage pipeline.
     * Connects to TakeDamage / health components downstream.
     */
    void ApplyWeaponDamage(AActor* Target, float Amount, const FHitResult& HitResult) const;

    /**
     * Launch the owning character.
     * @param bAdditive  true = add to existing velocity (preserves momentum).
     *                   false = replace velocity entirely (precise redirects).
     */
    void LaunchOwner(const FVector& Impulse, bool bAdditive = true) const;

    /** World-space forward vector of the owning character. */
    FVector GetOwnerForward() const;

    /** Current velocity from the CMC, or ZeroVector if no owner. */
    FVector GetOwnerVelocity() const;

    /** True when the owner's CMC is in a falling state. */
    bool IsOwnerAirborne() const;

private:
    /** Socket name on the character's skeleton that weapons attach to. */
    static const FName WeaponSocketName;
};
