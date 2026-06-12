#pragma once

#include "CoreMinimal.h"
#include "../../CruxCompatibility.h"
#include "Components/ActorComponent.h"
#include "CruxWeaponBase.h"
#include "CruxHotbarComponent.generated.h"

/**
 * UCruxHotbarComponent — Three-slot weapon manager
 * ==================================================
 * Attached to the player Character. Owns and manages all three weapon slots.
 * Handles equip / holster sequencing, input routing, and the frame-cancel swap.
 *
 * Slot layout (from GDD §4):
 *   Slot 1 (M3 click)  — Launcher / Verticality  → Mace / Axe
 *   Slot 2 (Scroll Up) — Bridger / Horizontal     → Sword / Spear
 *   Slot 3 (Scroll Dn) — Finisher / Ranged        → Bow / Crossbow
 *
 * Frame-canceling swap rules:
 *   - Zero animation delay. The new weapon is visible and usable immediately.
 *   - Fixed-scroll: scrolling past slot 3 does NOT wrap back to slot 1.
 *     You must scroll the other direction or press the direct slot key.
 *   - Both properties make the hotbar esports-viable without adding complexity.
 *
 * Weapon actors are spawned and held during the Character's lifetime.
 * They are not destroyed on holster — they persist attached but invisible.
 */

UCLASS(ClassGroup = (Crux), meta = (BlueprintSpawnableComponent))
class CRUX_API UCruxHotbarComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCruxHotbarComponent();

    // -------------------------------------------------------------------------
    // DEFAULT WEAPON ASSIGNMENTS
    // Set these in the Character Blueprint to choose which weapons spawn.
    // -------------------------------------------------------------------------

    /** Class to spawn for Slot 1 (Launcher — Mace/Axe). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crux|Hotbar")
    TSubclassOf<ACruxWeaponBase> Slot1WeaponClass;

    /** Class to spawn for Slot 2 (Bridger — Sword/Spear). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crux|Hotbar")
    TSubclassOf<ACruxWeaponBase> Slot2WeaponClass;

    /** Class to spawn for Slot 3 (Finisher — Bow/Crossbow). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crux|Hotbar")
    TSubclassOf<ACruxWeaponBase> Slot3WeaponClass;

    // -------------------------------------------------------------------------
    // PUBLIC API — bind these to your Character's input in SetupPlayerInputComponent
    // -------------------------------------------------------------------------

    /** Equip Slot 1 — bind to M3 (middle mouse click) */
    UFUNCTION(BlueprintCallable, Category = "Crux|Hotbar")
    void EquipSlot1();

    /** Equip Slot 2 — bind to Scroll Up */
    UFUNCTION(BlueprintCallable, Category = "Crux|Hotbar")
    void EquipSlot2();

    /** Equip Slot 3 — bind to Scroll Down */
    UFUNCTION(BlueprintCallable, Category = "Crux|Hotbar")
    void EquipSlot3();

    /**
     * Equip a slot directly. Holsters the current weapon first.
     * Frame-canceling: no delay, no transition animation.
     */
    UFUNCTION(BlueprintCallable, Category = "Crux|Hotbar")
    void EquipSlot(EWeaponSlot Slot);

    /** Route Left Click to the active weapon's PrimaryFire */
    UFUNCTION(BlueprintCallable, Category = "Crux|Hotbar")
    void PrimaryFire();

    /**
     * Route Right Click to the active weapon's SecondaryFire.
     * @param bHeld  true if held past CMC's MicroChargeHoldThreshold
     */
    UFUNCTION(BlueprintCallable, Category = "Crux|Hotbar")
    void SecondaryFire(bool bHeld);

    /** Get the currently equipped weapon actor */
    UFUNCTION(BlueprintPure, Category = "Crux|Hotbar")
    ACruxWeaponBase* GetActiveWeapon() const { return ActiveWeapon; }

    /** Get the currently active slot */
    UFUNCTION(BlueprintPure, Category = "Crux|Hotbar")
    EWeaponSlot GetActiveSlot() const { return ActiveSlot; }

    /** Get a weapon by slot index (for HUD display, UI, etc.) */
    UFUNCTION(BlueprintPure, Category = "Crux|Hotbar")
    ACruxWeaponBase* GetWeaponInSlot(EWeaponSlot Slot) const;

protected:
    virtual void BeginPlay() override;

private:
    /** Spawned weapon actors, indexed by EWeaponSlot cast to uint8 */
    UPROPERTY()
    TArray<ACruxWeaponBase*> Weapons; // [0]=Slot1, [1]=Slot2, [2]=Slot3

    UPROPERTY()
    ACruxWeaponBase* ActiveWeapon = nullptr;

    EWeaponSlot ActiveSlot = EWeaponSlot::Slot2_Bridger; // Start with Slot2 per GDD

    /** Spawn all three weapons and assign Slot1 as default */
    void SpawnWeapons();

    /** Spawn a single weapon of the given class, or nullptr if class is unset */
    ACruxWeaponBase* SpawnWeapon(TSubclassOf<ACruxWeaponBase> WeaponClass) const;

    /** Convert EWeaponSlot to array index */
    static int32 SlotToIndex(EWeaponSlot Slot) { return static_cast<int32>(Slot); }
};
