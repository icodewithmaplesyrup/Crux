#include "CruxHotbarComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

// ============================================================================
//  CONSTRUCTION
// ============================================================================

UCruxHotbarComponent::UCruxHotbarComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    // Pre-size the weapon array to exactly 3 slots (indexed by EWeaponSlot)
    Weapons.SetNum(3);
}

void UCruxHotbarComponent::BeginPlay()
{
    Super::BeginPlay();
    SpawnWeapons();

    // Default to Slot 2 per GDD — the Sword/Spear slot is the starting weapon
    EquipSlot(EWeaponSlot::Slot2_Bridger);
}

// ============================================================================
//  WEAPON SPAWNING
// ============================================================================

void UCruxHotbarComponent::SpawnWeapons()
{
    Weapons[0] = SpawnWeapon(Slot1WeaponClass);
    Weapons[1] = SpawnWeapon(Slot2WeaponClass);
    Weapons[2] = SpawnWeapon(Slot3WeaponClass);
}

ACruxWeaponBase* UCruxHotbarComponent::SpawnWeapon(
    TSubclassOf<ACruxWeaponBase> WeaponClass) const
{
    if (!WeaponClass || !GetWorld()) return nullptr;

    ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
    if (!OwnerChar) return nullptr;

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner      = OwnerChar;
    SpawnParams.Instigator = OwnerChar->GetInstigator();
    // SpawnAlwaysInWorld: weapon always exists, even if it clips geometry at spawn
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ACruxWeaponBase* NewWeapon = GetWorld()->SpawnActor<ACruxWeaponBase>(
        WeaponClass,
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParams);

    return NewWeapon;
}

// ============================================================================
//  EQUIP — frame-canceling weapon swap
// ============================================================================
//
//  "Frame-canceling (zero animation delay when swapping mid-air to chain
//  momentum)." — GDD §4
//
//  This function is the entire equip system. It:
//    1. Holsters the current weapon (hides it, marks it inactive)
//    2. Sets the new active weapon
//    3. Calls Equip() on the new weapon (shows it, attaches it)
//
//  There are no animation states, no transition windows, no input locks.
//  The new weapon is active and fireable on the same frame this is called.
//
// ============================================================================

void UCruxHotbarComponent::EquipSlot(EWeaponSlot Slot)
{
    const int32 Index = SlotToIndex(Slot);
    if (!Weapons.IsValidIndex(Index)) return;

    ACruxWeaponBase* NewWeapon = Weapons[Index];
    if (!NewWeapon || NewWeapon == ActiveWeapon) return; // Already equipped

    // ── 1. Holster current weapon ─────────────────────────────────────────────
    if (ActiveWeapon)
        ActiveWeapon->Holster();

    // ── 2. Update state ───────────────────────────────────────────────────────
    ActiveWeapon = NewWeapon;
    ActiveSlot   = Slot;

    // ── 3. Equip new weapon ───────────────────────────────────────────────────
    ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
    if (OwnerChar)
        ActiveWeapon->Equip(OwnerChar);
}

void UCruxHotbarComponent::EquipSlot1() { EquipSlot(EWeaponSlot::Slot1_Launcher); }
void UCruxHotbarComponent::EquipSlot2() { EquipSlot(EWeaponSlot::Slot2_Bridger);  }
void UCruxHotbarComponent::EquipSlot3() { EquipSlot(EWeaponSlot::Slot3_Finisher); }

// ============================================================================
//  FIRE ROUTING
// ============================================================================
//
//  The Character class should bind input directly to these functions.
//  HotbarComponent acts as a router — it forwards calls to whichever
//  weapon is currently active. No switch statements needed in the Character.
//
//  Suggested input bindings in ACharacterCrux::SetupPlayerInputComponent():
//
//    PlayerInputComponent->BindAction("PrimaryFire", IE_Pressed,
//        HotbarComponent, &UCruxHotbarComponent::PrimaryFire);
//    PlayerInputComponent->BindAction("SecondaryFire_Tap", IE_Released,
//        HotbarComponent, &UCruxHotbarComponent::SecondaryFire, false);
//    PlayerInputComponent->BindAction("SecondaryFire_Hold", IE_Pressed,
//        HotbarComponent, &UCruxHotbarComponent::SecondaryFire, true);
//    PlayerInputComponent->BindAction("Slot1", IE_Pressed,
//        HotbarComponent, &UCruxHotbarComponent::EquipSlot1);
//    ... etc.
//
// ============================================================================

void UCruxHotbarComponent::PrimaryFire()
{
    if (ActiveWeapon)
        ActiveWeapon->PrimaryFire();
}

void UCruxHotbarComponent::SecondaryFire(bool bHeld)
{
    if (ActiveWeapon)
        ActiveWeapon->SecondaryFire(bHeld);
}

ACruxWeaponBase* UCruxHotbarComponent::GetWeaponInSlot(EWeaponSlot Slot) const
{
    const int32 Index = SlotToIndex(Slot);
    return Weapons.IsValidIndex(Index) ? Weapons[Index] : nullptr;
}
