//------------------------------------------------------------------------------
// STC_RevolverCylinderComponent
// Visual cylinder indexing for a magazine-fed revolver (RG-6). Goes on the
// CYLINDER MAGAZINE prefab. The magazine mesh origin must sit on the cylinder's
// rotation axis with the axis along the model's +Z (forward); every spent round
// turns the attached cylinder m_fDegreesPerRound about that axis.
//
// HOW: through the weapon slot the cylinder hangs in (the muzzle's
// MagazinePosition InventoryStorageSlot) with EntitySlotInfo.SetAdditiveTransformLS.
// The slot combines the matrix with its own Offset/Angles, keeps it while this
// entity stays attached, and discards it when another entity is attached - so a
// freshly inserted cylinder always starts from the slot's own transform.
//
// WHEN: the angle is DERIVED from the replicated ammo count, never accumulated
// from events, so a missed or duplicated event cannot desync it. Events only wake
// the component up:
//   - SCR_MuzzleEffectComponent.GetOnWeaponFired()  (weapon-local muzzle effect)
//   - holder's EventHandlerManagerComponent "OnAmmoCountChanged"
//   - InventoryItemComponent.m_OnParentSlotChangedInvoker (insert / reload / detach)
// After a wake-up it ticks through the call queue for a short watch window (a
// remote client can see the shot a few frames before the new ammo count), tweens
// to the target and removes itself. Idle cost is zero; dedicated servers skip it.
//------------------------------------------------------------------------------
[ComponentEditorProps(category: "STC Modules", description: "Revolver cylinder indexing: turns the attached cylinder magazine about its own forward axis by a fixed angle per spent round. Visual only, client side, woken by fire/ammo events, no per-frame work while idle.")]
class STC_RevolverCylinderComponentClass : ScriptComponentClass
{
}

class STC_RevolverCylinderComponent : ScriptComponent
{
	[Attribute("60", UIWidgets.EditBox, "Degrees the cylinder turns per spent round (360 / number of chambers)")]
	protected float m_fDegreesPerRound;

	[Attribute("0", UIWidgets.CheckBox, "Turn the other way")]
	protected bool m_bReverse;

	[Attribute("0.1", UIWidgets.EditBox, "Seconds one index step takes")]
	protected float m_fIndexTime;

	[Attribute("0.5", UIWidgets.EditBox, "Seconds to keep re-reading the ammo count after a wake-up (late ammo replication on remote clients)")]
	protected float m_fWatchTime;

	[Attribute("0", UIWidgets.CheckBox, "Count a round in the weapon's chamber as still in the cylinder. Only for muzzles with ChamberedAmmo 1; leave off for the RG-6 (ChamberedAmmo 0).")]
	protected bool m_bCountChamberedRound;

	[Attribute("0", UIWidgets.CheckBox, "Fallback: write the cylinder entity's local transform instead of the slot's additive transform. Only exact while MagazinePosition Offset and Angles are zero.")]
	protected bool m_bDriveEntityDirectly;

	protected BaseMagazineComponent m_Magazine;
	protected InventoryItemComponent m_Item;

	// Only set while the cylinder sits in a weapon.
	protected InventoryStorageSlot m_Slot;
	protected InventoryItemComponent m_WeaponItem;
	protected BaseWeaponComponent m_Weapon;
	protected SCR_MuzzleEffectComponent m_MuzzleEffect;
	protected EventHandlerManagerComponent m_HolderEvents;

	protected float m_fShown;		// degrees currently wanted on screen, 0..360
	protected float m_fFrom;
	protected float m_fTo;
	protected float m_fMoveStart;	// world time [ms]
	protected float m_fMoveLength;	// [ms]
	protected float m_fWatchUntil;	// world time [ms]
	protected bool m_bMoving;
	protected bool m_bTicking;
	protected bool m_bNeedApply;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		// Purely cosmetic: a dedicated server never draws it.
		if (System.IsConsoleApp())
			return;

		if (!GetGame() || !GetGame().InPlayMode())
			return;

		if (m_fDegreesPerRound <= 0)
			return;

		m_Magazine = BaseMagazineComponent.Cast(owner.FindComponent(BaseMagazineComponent));
		m_Item = InventoryItemComponent.Cast(owner.FindComponent(InventoryItemComponent));
		if (!m_Magazine || !m_Item)
		{
			m_Magazine = null;
			m_Item = null;
			return;
		}

		// Subscribe now (all components exist at OnPostInit), so an attach that happens before
		// EOnInit - a weapon spawned with this cylinder in it - is not missed.
		m_Item.m_OnParentSlotChangedInvoker.Insert(OnCylinderSlotChanged);

		SetEventMask(owner, EntityEvent.INIT);

		// Safety net that does not depend on EOnInit: bind to whatever slot we are in by then
		// and pick up an ammo count that a joining client received after init.
		GetGame().GetCallqueue().CallLater(Resync, 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		if (!m_Item)
			return;

		InventoryStorageSlot slot = m_Item.GetParentSlot();
		if (slot != m_Slot)
			BindToSlot(slot);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (GetGame())
			GetGame().GetCallqueue().Remove(Resync);

		UnbindWeapon();

		if (m_Item)
			m_Item.m_OnParentSlotChangedInvoker.Remove(OnCylinderSlotChanged);

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Delayed one-shot after init.
	protected void Resync()
	{
		if (!m_Item)
			return;

		InventoryStorageSlot slot = m_Item.GetParentSlot();
		if (slot != m_Slot)
		{
			BindToSlot(slot);
			return;
		}

		if (!m_Slot)
			return;

		// Late ammo count on a joining client: jump to it instead of spinning through chambers.
		if (!m_bMoving)
			SetShown(TargetAngle());

		Wake();
	}

	//------------------------------------------------------------------------------------------------
	//! The cylinder moved: into a weapon, out of it (reload / detach), into a pouch, onto the ground.
	protected void OnCylinderSlotChanged(InventoryStorageSlot oldSlot, InventoryStorageSlot newSlot)
	{
		BindToSlot(newSlot);
	}

	//------------------------------------------------------------------------------------------------
	protected void BindToSlot(InventoryStorageSlot slot)
	{
		UnbindWeapon();
		if (!slot)
			return;

		BaseInventoryStorageComponent storage = slot.GetStorage();
		if (!storage)
			return;

		IEntity weaponEnt = storage.GetOwner();
		if (!weaponEnt)
			return;

		BaseWeaponComponent weapon = BaseWeaponComponent.Cast(weaponEnt.FindComponent(BaseWeaponComponent));
		if (!weapon)
			return; // a pouch, a crate, a hand during reload - not a weapon

		m_Slot = slot;
		m_Weapon = weapon;

		m_MuzzleEffect = SCR_MuzzleEffectComponent.Cast(weaponEnt.FindComponent(SCR_MuzzleEffectComponent));
		if (m_MuzzleEffect)
			m_MuzzleEffect.GetOnWeaponFired().Insert(OnShot);

		// The launcher's own item component tells us when the launcher changes hands.
		m_WeaponItem = InventoryItemComponent.Cast(weaponEnt.FindComponent(InventoryItemComponent));
		if (!m_WeaponItem)
			m_WeaponItem = storage;

		m_WeaponItem.m_OnParentSlotChangedInvoker.Insert(OnWeaponSlotChanged);
		BindHolder(FindCharacter(weaponEnt));

		// The slot has just put us at its own transform; restore the index for the current ammo.
		SetShown(TargetAngle());
		Wake();
	}

	//------------------------------------------------------------------------------------------------
	protected void UnbindWeapon()
	{
		StopTicking();

		if (m_MuzzleEffect)
			m_MuzzleEffect.GetOnWeaponFired().Remove(OnShot);

		if (m_WeaponItem)
			m_WeaponItem.m_OnParentSlotChangedInvoker.Remove(OnWeaponSlotChanged);

		BindHolder(null);

		m_MuzzleEffect = null;
		m_WeaponItem = null;
		m_Weapon = null;
		m_Slot = null;
		m_bMoving = false;
		m_bNeedApply = false;
	}

	//------------------------------------------------------------------------------------------------
	//! The launcher itself moved (picked up, slung, dropped): follow its holder's event manager.
	protected void OnWeaponSlotChanged(InventoryStorageSlot oldSlot, InventoryStorageSlot newSlot)
	{
		IEntity holder;
		if (newSlot && newSlot.GetStorage())
			holder = newSlot.GetStorage().GetOwner();

		BindHolder(FindCharacter(holder));
	}

	//------------------------------------------------------------------------------------------------
	//! Nearest character at or above ent. Not GetRootParent(): that is the vehicle when the holder is seated.
	protected ChimeraCharacter FindCharacter(IEntity ent)
	{
		while (ent)
		{
			ChimeraCharacter character = ChimeraCharacter.Cast(ent);
			if (character)
				return character;

			ent = ent.GetParent();
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected void BindHolder(IEntity holder)
	{
		if (m_HolderEvents)
			m_HolderEvents.RemoveScriptHandler("OnAmmoCountChanged", this, OnAmmoCountChanged);

		m_HolderEvents = null;

		if (!holder || !ChimeraCharacter.Cast(holder))
			return;

		m_HolderEvents = EventHandlerManagerComponent.Cast(holder.FindComponent(EventHandlerManagerComponent));
		if (m_HolderEvents)
			m_HolderEvents.RegisterScriptHandler("OnAmmoCountChanged", this, OnAmmoCountChanged);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnShot(IEntity effectEntity, BaseMuzzleComponent muzzle, IEntity projectileEntity)
	{
		Wake();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnAmmoCountChanged(BaseWeaponComponent weapon, BaseMuzzleComponent muzzle, BaseMagazineComponent magazine, int ammoCount, bool isBarrelChambered)
	{
		if (magazine == m_Magazine)
			Wake();
	}

	//------------------------------------------------------------------------------------------------
	protected void Wake()
	{
		if (!m_Slot || !m_Magazine)
			return;

		m_fWatchUntil = WorldTimeMs() + m_fWatchTime * 1000;
		if (m_bTicking)
			return;

		m_bTicking = true;
		GetGame().GetCallqueue().CallLater(Tick, 0, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void StopTicking()
	{
		if (!m_bTicking)
			return;

		m_bTicking = false;
		if (GetGame())
			GetGame().GetCallqueue().Remove(Tick);
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		if (!m_Slot || !m_Magazine)
		{
			StopTicking();
			return;
		}

		float now = WorldTimeMs();
		float target = TargetAngle();

		if (Math.AbsFloat(target - m_fTo) > 0.01)
		{
			if (target < m_fTo)
			{
				// Refilled or reset in place: never spin backwards through the chambers, just jump
				// (every multiple of one chamber looks the same).
				SetShown(target);
			}
			else
			{
				m_fFrom = m_fShown;
				m_fTo = target;
				m_fMoveStart = now;
				// Catching up several rounds at once (lag, late join) takes longer, capped at 3 steps.
				float steps = Math.Clamp((m_fTo - m_fFrom) / m_fDegreesPerRound, 1, 3);
				m_fMoveLength = Math.Max(m_fIndexTime * 1000 * steps, 1);
				m_bMoving = true;
				m_fWatchUntil = Math.Max(m_fWatchUntil, now + m_fMoveLength);
			}
		}

		if (m_bMoving)
		{
			float t = Math.Clamp((now - m_fMoveStart) / m_fMoveLength, 0, 1);
			float inv = 1.0 - t;
			float ease = 1.0 - inv * inv * inv; // ease-out cubic
			m_fShown = m_fFrom + (m_fTo - m_fFrom) * ease;
			if (t >= 1)
			{
				m_fShown = m_fTo;
				m_bMoving = false;
			}

			m_bNeedApply = !ApplyAngle(m_fShown);
			return;
		}

		if (m_bNeedApply)
			m_bNeedApply = !ApplyAngle(m_fShown);

		if (now >= m_fWatchUntil)
			StopTicking();
	}

	//------------------------------------------------------------------------------------------------
	protected void SetShown(float angle)
	{
		m_fFrom = angle;
		m_fTo = angle;
		m_fShown = angle;
		m_bMoving = false;
		m_bNeedApply = !ApplyAngle(angle);
	}

	//------------------------------------------------------------------------------------------------
	//! Degrees for the rounds already gone: 0 with a full cylinder, 360 when empty.
	protected float TargetAngle()
	{
		int maxAmmo = m_Magazine.GetMaxAmmoCount();
		int left = m_Magazine.GetAmmoCount();

		if (m_bCountChamberedRound && m_Weapon)
		{
			BaseMuzzleComponent muzzle = m_Weapon.GetCurrentMuzzle();
			// Chamber state only means something on a muzzle that can chamber (vanilla SCR_AIUtils guard).
			if (muzzle && muzzle.GetMagazine() == m_Magazine && muzzle.IsChamberingPossible() && muzzle.IsCurrentBarrelChambered())
				left++;
		}

		float spent = Math.ClampInt(maxAmmo - left, 0, maxAmmo);
		return spent * m_fDegreesPerRound;
	}

	//------------------------------------------------------------------------------------------------
	//! Rotation about the cylinder's own +Z axis. Built explicitly so no Euler-order convention is involved.
	protected bool ApplyAngle(float angle)
	{
		IEntity owner = GetOwner();
		if (!m_Slot || !owner || m_Slot.GetAttachedEntity() != owner)
			return false; // mid-swap: never turn somebody else's cylinder

		float rad = angle * Math.DEG2RAD;
		if (m_bReverse)
			rad = -rad;

		float c = Math.Cos(rad);
		float s = Math.Sin(rad);

		vector mat[4];
		mat[0] = Vector(c, s, 0);
		mat[1] = Vector(-s, c, 0);
		mat[2] = Vector(0, 0, 1);
		mat[3] = vector.Zero;

		if (m_bDriveEntityDirectly)
		{
			owner.SetLocalTransform(mat);
			owner.Update();
		}
		else
		{
			m_Slot.SetAdditiveTransformLS(mat);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected float WorldTimeMs()
	{
		if (!GetGame() || !GetGame().GetWorld())
			return 0;

		return GetGame().GetWorld().GetWorldTime();
	}
}
