//------------------------------------------------------------------------------------------------
// STC_MortarCellVisualComponent
// Firing animation for the 50 mm five-round revolver mortar cell. Goes on the CELL MAGAZINE prefab
// (Magazine_Mortar50_HE.et; Magazine_Mortar50_Smoke / _Airburst inherit it, same-GUID overrides only).
//
// WHAT
//   While the cell sits in the mortar's weapon (the muzzle's MagazinePosition slot):
//   - the cell mesh is swapped to Mortar50_CellEmpty.xob (slotted chambers, no baked bombs) and one client-local
//     bomb entity (Mortar50_ShellVisual.et: MeshObject Shell50 InheritVisibility 1 + Hierarchy, no RplComponent)
//     is spawned as a child of the cell in every loaded chamber;
//   - on each shot the bomb in line with the barrel slides 0.42 m toward the muzzle in 0.15 s (accelerating) and
//     is deleted; 0.05 s later the cell turns one chamber (72 deg) in 0.30 s (ease-out cubic).
//   A loose cell (ground, turret storage, backpack) shows the prefab model (five baked bombs), or the empty model
//   once no round is left (m_bEmptyLookWhenSpent).
//
// FRAME (cell-local, engine axes): origin on the cylinder axis at the rear face, axis +Y (parallel to the bore,
//   MagazinePosition has no Angles), chamber k centre (R sin 72k, y, -R cos 72k), R 0.068. Chamber 0 (-Z) is in
//   line with the bore when the slot has just attached the cell. Bomb seat: Shell50 origin at y 0.165 (nose 0.310),
//   bomb rows (1,0,0) (0,0,-1) (0,1,0) = nose along cell +Y.
//
// STATE is derived from the replicated ammo count. Shot events (SCR_MuzzleEffectComponent.GetOnWeaponFired) wake
//   the component up and may run the view ahead of a lagging count by the shots actually seen; a count drop and a
//   shot event inside m_fMatchWindow are one shot. Reload / unload / partial cells / late join jump straight to the
//   right chamber (snap). Visual only: nothing is replicated, a console (dedicated) server returns in OnPostInit,
//   and nothing runs per frame while idle (frame tick only during a step and a short watch window; a 1 Hz poll
//   while mounted re-applies the angle and catches missed counts).
//
// MOUNTED TEST (vanilla SCR_DetachMagazineUserAction): the parent slot's storage is a WeaponAttachmentsStorageComponent
//   whose owner carries a BaseWeaponComponent that is not a WeaponSlotComponent. Cells in the turret's own storage
//   (the turret also has a WeaponSlotComponent) or in any character storage do NOT bind.
//
// UNVERIFIED in game: EntitySlotInfo.SetAdditiveTransformLS turning the cell about its own origin (fallback
//   m_bDriveEntityDirectly); SetObject on a slotted magazine staying in effect (re-asserted when provably reverted);
//   the muzzle event on remote proxies; OnAmmoCountChanged reaching the turret's EventHandlerManagerComponent.
//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "STC Modules", description: "Revolver mortar cell: shows the bombs in the loaded chambers while mounted, slides the fired bomb into the barrel and turns the cell one chamber per shot. Client-side visual, event driven, no ticking while idle.")]
class STC_MortarCellVisualComponentClass : ScriptComponentClass
{
}

class STC_MortarCellVisualComponent : ScriptComponent
{
	// ---------------------------------------------------------------- geometry (cell-local, metres)
	[Attribute("0.068", UIWidgets.EditBox, "Distance of the chamber axes from the cylinder axis [m]", category: "Geometry")]
	protected float m_fChamberRadius;

	[Attribute("0", UIWidgets.EditBox, "Degrees between chambers; 0 = 360 / MaxAmmo", category: "Geometry")]
	protected float m_fDegreesPerChamber;

	[Attribute("1", UIWidgets.CheckBox, "Firing order: the chamber on the gunner's side (-X, where operator_idle_01 kneels) rises into the barrel next (chambers 0,4,3,2,1). Off = the +X chamber rises (0,1,2,3,4).", category: "Geometry")]
	protected bool m_bRiseFromLeft;

	[Attribute("0.165", UIWidgets.EditBox, "Bomb origin along the cell axis when chambered [m]. 0.165 puts the Shell50 nose at 0.310, where the baked bombs of Mortar50_Cell have theirs.", category: "Geometry")]
	protected float m_fShellSeat;

	[Attribute("0.42", UIWidgets.EditBox, "How far the fired bomb slides toward the muzzle before it is removed [m]. Must carry its tail (0.19 m behind the origin) past the barrel entry.", category: "Geometry")]
	protected float m_fSlideDistance;

	// ---------------------------------------------------------------- timing
	[Attribute("0.15", UIWidgets.EditBox, "Seconds the fired bomb takes to slide into the barrel", category: "Timing")]
	protected float m_fSlideTime;

	[Attribute("0.05", UIWidgets.EditBox, "Seconds between the bomb vanishing and the cell starting to turn", category: "Timing")]
	protected float m_fRotateDelay;

	[Attribute("0.3", UIWidgets.EditBox, "Seconds one chamber turn takes (ease-out)", category: "Timing")]
	protected float m_fRotateTime;

	[Attribute("1.5", UIWidgets.EditBox, "Seconds to keep reading the ammo count after a wake-up (late replication on remote clients)", category: "Timing")]
	protected float m_fWatchTime;

	[Attribute("0.25", UIWidgets.EditBox, "Shot events closer than this are one shot [s] (the weapon cycles every 1.5 s at 40 rpm)", category: "Timing")]
	protected float m_fShotDebounce;

	[Attribute("1", UIWidgets.EditBox, "A count drop and a shot event closer than this are the same shot [s]", category: "Timing")]
	protected float m_fMatchWindow;

	[Attribute("4", UIWidgets.EditBox, "How long a shot event may keep the view ahead of the replicated ammo count [s]. Once the count is known to replicate, a view still ahead after this is corrected.", category: "Timing")]
	protected float m_fPredictTimeout;

	[Attribute("1.2", UIWidgets.EditBox, "Count drops this soon after binding that no shot event claims within the match window are jumped to, not animated [s] (late join, late count after reload)", category: "Timing")]
	protected float m_fSettleSeconds;

	[Attribute("2", UIWidgets.EditBox, "Rounds behind that are still animated one after the other; more are snapped (lag)", category: "Timing")]
	protected int m_iMaxAnimatedSteps;

	[Attribute("1", UIWidgets.CheckBox, "Start the slide on the shot event instead of waiting for the ammo count", category: "Timing")]
	protected bool m_bPredictOnShot;

	[Attribute("1", UIWidgets.EditBox, "Poll while mounted [s]: re-applies the cell angle and catches ammo counts no event reported; 0 = off", category: "Timing")]
	protected float m_fIdlePollSeconds;

	// ---------------------------------------------------------------- resources
	[Attribute("{5C1A77E3B0A956C6}Prefabs/Weapons/Mortars/BFA/Mortar50_ShellVisual.et", UIWidgets.ResourcePickerThumbnail, "Local visual prefab of one bomb: GenericEntity + MeshObject (InheritVisibility 1) + Hierarchy, NO RplComponent. Nose along +Z, origin at the body centre.", params: "et", category: "Resources")]
	protected ResourceName m_sShellPrefab;

	[Attribute("{5C1A77E3B0A956C5}Assets/Weapons/Mortar50/Mortar50_CellEmpty.xob", UIWidgets.ResourcePickerThumbnail, "Cell model with empty (slotted) chambers, shown while mounted. Empty = keep the prefab model.", params: "xob", category: "Resources")]
	protected ResourceName m_sEmptyCellModel;

	[Attribute("1", UIWidgets.CheckBox, "A loose cell with no round left shows the empty cell model as well", category: "Resources")]
	protected bool m_bEmptyLookWhenSpent;

	[Attribute("0", UIWidgets.CheckBox, "Fallback: write the cell entity's local transform (slot transform x rotation) instead of the slot's additive transform. Use only if the cell orbits instead of turning about its axis.", category: "Resources")]
	protected bool m_bDriveEntityDirectly;

	[Attribute("0", UIWidgets.CheckBox, "Log binds, shots, count drops and steps to the console (testing)", category: "Debug")]
	protected bool m_bDebug;

	// ---------------------------------------------------------------- state
	protected static const int PHASE_IDLE = 0;
	protected static const int PHASE_SLIDE = 1;
	protected static const int PHASE_PAUSE = 2;
	protected static const int PHASE_ROTATE = 3;
	protected static const float NEVER = -1000000;

	protected BaseMagazineComponent m_Magazine;
	protected InventoryItemComponent m_Item;

	protected ref Resource m_ShellPrefabResource;
	protected ref Resource m_EmptyCellResource;
	protected ref Resource m_LoadedCellResource;
	protected VObject m_EmptyCellVObject;
	protected VObject m_LoadedCellVObject;
	protected VObject m_CurrentModel;			// model this component last set; null = never touched (prefab model)
	protected bool m_bModelCompareWorks;		// GetVObject() == the handle we set, proven right after a SetObject

	// only while mounted in a weapon
	protected InventoryStorageSlot m_Slot;
	protected IEntity m_WeaponEntity;
	protected InventoryItemComponent m_WeaponItem;
	protected ref array<SCR_MuzzleEffectComponent> m_aMuzzleEffects = {};
	protected EventHandlerManagerComponent m_TurretEvents;
	protected IEntity m_TurretEventsOwner;
	protected ref array<IEntity> m_aShells = {};	// index = chamber, null = empty chamber

	protected int m_iMax = 1;
	protected float m_fPitch = 72;
	protected int m_iShownSpent;				// rounds already discharged on screen (0..max)
	protected int m_iLastCount;					// last ammo count read
	protected int m_iEventSpent;				// spent count implied by shot events and counts (high-water mark)
	protected float m_fPredictUntil = NEVER;	// world time [ms] until m_iEventSpent may lead the count
	protected int m_iUnmatchedDrops;			// count drops no shot event has claimed yet
	protected float m_fUnmatchedDropTime = NEVER;
	protected bool m_bCountLive;				// a count drop was seen while bound: the count replicates here
	protected float m_fLastShotTime = NEVER;
	protected float m_fBindTime = NEVER;

	protected int m_iPhase;
	protected float m_fPhaseStart;
	protected float m_fStepTimeScale = 1;
	protected int m_iAnimChamber;
	protected float m_fAngleFrom;
	protected float m_fAngleTo;
	protected float m_fShownAngle;
	protected float m_fWatchUntil = NEVER;
	protected bool m_bTicking;
	protected bool m_bPolling;
	protected bool m_bNeedApply;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		// Purely cosmetic: a console (dedicated) server renders nothing.
		if (System.IsConsoleApp())
			return;

		if (!GetGame() || !GetGame().InPlayMode())
			return;

		m_Magazine = BaseMagazineComponent.Cast(owner.FindComponent(BaseMagazineComponent));
		m_Item = InventoryItemComponent.Cast(owner.FindComponent(InventoryItemComponent));
		if (!m_Magazine || !m_Item)
		{
			m_Magazine = null;
			m_Item = null;
			return;
		}

		LoadResources(owner);

		// Subscribe now (all components exist at OnPostInit), so an attach before EOnInit is not missed.
		m_Item.m_OnParentSlotChangedInvoker.Insert(OnCellSlotChanged);
		SetEventMask(owner, EntityEvent.INIT);

		// Safety net independent of EOnInit: bind to the slot we are in by then, pick up a late ammo count.
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
		else if (!m_Slot)
			ApplyLooseLook();
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (GetGame())
			GetGame().GetCallqueue().Remove(Resync);

		Unbind();

		if (m_Item)
			m_Item.m_OnParentSlotChangedInvoker.Remove(OnCellSlotChanged);

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void LoadResources(IEntity owner)
	{
		// ref Resource members keep the prefab and models loaded; SetObject adds its own reference per entity
		if (!m_sShellPrefab.IsEmpty())
		{
			m_ShellPrefabResource = Resource.Load(m_sShellPrefab);
			if (m_ShellPrefabResource && !m_ShellPrefabResource.IsValid())
				m_ShellPrefabResource = null;
		}

		if (!m_sEmptyCellModel.IsEmpty())
		{
			m_EmptyCellResource = Resource.Load(m_sEmptyCellModel);
			m_EmptyCellVObject = LoadVObject(m_EmptyCellResource);
		}

		// the prefab's own model = the loaded look restored on a loose cell (vanilla SCR_ConsumableItemComponent)
		ResourceName loadedModel = SCR_Global.GetPrefabAttributeResource(owner, "MeshObject", "Object");
		if (!loadedModel.IsEmpty())
		{
			m_LoadedCellResource = Resource.Load(loadedModel);
			m_LoadedCellVObject = LoadVObject(m_LoadedCellResource);
		}

		// never swap without a way back
		if (!m_LoadedCellVObject)
			m_EmptyCellVObject = null;
	}

	//------------------------------------------------------------------------------------------------
	protected VObject LoadVObject(Resource res)
	{
		if (!res || !res.IsValid())
			return null;

		BaseResourceObject resObject = res.GetResource();
		if (!resObject)
			return null;

		return resObject.ToVObject();
	}

	//------------------------------------------------------------------------------------------------
	//! Delayed one-shot after init: bind to whatever slot we are in and pick up a late ammo count.
	protected void Resync()
	{
		if (!m_Item || !m_Magazine)
			return;

		InventoryStorageSlot slot = m_Item.GetParentSlot();
		if (slot != m_Slot)
		{
			BindToSlot(slot);
			return;
		}

		if (!m_Slot)
		{
			ApplyLooseLook();
			return;
		}

		if (!m_TurretEvents)
			BindTurretEvents(m_WeaponEntity);

		Wake();
	}

	//------------------------------------------------------------------------------------------------
	//! The cell moved: into a weapon, out of it (reload / unload), into a storage, onto the ground.
	protected void OnCellSlotChanged(InventoryStorageSlot oldSlot, InventoryStorageSlot newSlot)
	{
		BindToSlot(newSlot);
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla SCR_DetachMagazineUserAction test: a magazine in a weapon has GetParentSlot().GetStorage() = the
	//! weapon's WeaponAttachmentsStorageComponent. Storages owned by a turret (WeaponSlotComponent is a
	//! BaseWeaponComponent too) or a character never count as mounted.
	//! Review fix: the component is looked up by the base class, so the WeaponSlotComponent guard below is reachable
	//! (WeaponComponent and WeaponSlotComponent are siblings: FindComponent(WeaponComponent) never returns a turret
	//! slot, which made the guard dead code). Vanilla precedent for the wide lookup: SCR_AIDecoHasWeaponOfType.
	protected BaseWeaponComponent FindMountingWeapon(InventoryStorageSlot slot)
	{
		if (!slot)
			return null;

		WeaponAttachmentsStorageComponent storage = WeaponAttachmentsStorageComponent.Cast(slot.GetStorage());
		if (!storage)
			return null;

		IEntity weaponEnt = storage.GetOwner();
		if (!weaponEnt)
			return null;

		BaseWeaponComponent weapon = BaseWeaponComponent.Cast(weaponEnt.FindComponent(BaseWeaponComponent));
		if (!weapon || WeaponSlotComponent.Cast(weapon))
			return null;

		return weapon;
	}

	//------------------------------------------------------------------------------------------------
	protected void BindToSlot(InventoryStorageSlot slot)
	{
		Unbind();

		BaseWeaponComponent weapon = FindMountingWeapon(slot);
		if (!weapon)
		{
			ApplyLooseLook();
			if (m_bDebug && slot)
				DebugLog("loose (not a weapon magazine slot)");

			return;
		}

		m_Slot = slot;
		m_WeaponEntity = weapon.GetOwner();
		m_iMax = m_Magazine.GetMaxAmmoCount();
		if (m_iMax < 1)
			m_iMax = 1;

		m_fPitch = m_fDegreesPerChamber;
		if (m_fPitch <= 0)
			m_fPitch = 360.0 / m_iMax;

		// shot events: SCR_MuzzleEffectComponent sits under the muzzle (vanilla mortar tutorial:
		// muzzleComponent.FindComponent(SCR_MuzzleEffectComponent))
		array<BaseMuzzleComponent> muzzles = {};
		weapon.GetMuzzlesList(muzzles);
		foreach (BaseMuzzleComponent muzzle : muzzles)
		{
			if (!muzzle)
				continue;

			AddMuzzleEffect(SCR_MuzzleEffectComponent.Cast(muzzle.FindComponent(SCR_MuzzleEffectComponent)));
		}

		if (m_aMuzzleEffects.IsEmpty() && m_WeaponEntity)
			AddMuzzleEffect(SCR_MuzzleEffectComponent.Cast(m_WeaponEntity.FindComponent(SCR_MuzzleEffectComponent)));

		// the weapon's storage is its item component too: follow it when it is moved to another turret
		m_WeaponItem = slot.GetStorage();
		if (m_WeaponItem)
			m_WeaponItem.m_OnParentSlotChangedInvoker.Insert(OnWeaponSlotChanged);

		BindTurretEvents(m_WeaponEntity);

		m_aShells.Clear();
		for (int i = 0; i < m_iMax; i++)
		{
			m_aShells.Insert(null);
		}

		m_fBindTime = NowMs();
		m_bCountLive = false;
		m_fPredictUntil = NEVER;
		m_fUnmatchedDropTime = NEVER;
		ShowCellModel(m_EmptyCellVObject, false);
		SnapToSpent(SpentFromCount(m_Magazine.GetAmmoCount()), false);

		if (m_fIdlePollSeconds > 0)
		{
			m_bPolling = true;
			GetGame().GetCallqueue().CallLater(Poll, Math.Max(m_fIdlePollSeconds * 1000, 100), true);
		}

		if (m_bDebug)
			DebugLog("bound to weapon");

		Wake();
	}

	//------------------------------------------------------------------------------------------------
	protected void AddMuzzleEffect(SCR_MuzzleEffectComponent effect)
	{
		if (!effect || m_aMuzzleEffects.Find(effect) >= 0)
			return;

		effect.GetOnWeaponFired().Insert(OnShot);
		m_aMuzzleEffects.Insert(effect);
	}

	//------------------------------------------------------------------------------------------------
	protected void Unbind()
	{
		StopTicking();

		if (m_bPolling && GetGame())
			GetGame().GetCallqueue().Remove(Poll);

		m_bPolling = false;

		// the weapon may be going away together with this cell (dismantle): then its invokers die with it
		bool weaponAlive = m_WeaponEntity && !m_WeaponEntity.IsDeleted();
		if (weaponAlive)
		{
			foreach (SCR_MuzzleEffectComponent effect : m_aMuzzleEffects)
			{
				if (effect)
					effect.GetOnWeaponFired().Remove(OnShot);
			}

			if (m_WeaponItem)
				m_WeaponItem.m_OnParentSlotChangedInvoker.Remove(OnWeaponSlotChanged);
		}

		m_aMuzzleEffects.Clear();
		m_WeaponItem = null;

		BindTurretEvents(null);
		DeleteAllShells();
		m_aShells.Clear();

		m_Slot = null;
		m_WeaponEntity = null;
		m_iPhase = PHASE_IDLE;
		m_fPredictUntil = NEVER;
		m_iUnmatchedDrops = 0;
		m_bNeedApply = false;
	}

	//------------------------------------------------------------------------------------------------
	//! The weapon itself moved: follow the event manager of whatever now holds it.
	protected void OnWeaponSlotChanged(InventoryStorageSlot oldSlot, InventoryStorageSlot newSlot)
	{
		BindTurretEvents(m_WeaponEntity);
	}

	//------------------------------------------------------------------------------------------------
	//! Turret_Base carries EventHandlerManagerComponent {551EC6A46DA1232A}; vehicle weapon HUDs listen there.
	protected void BindTurretEvents(IEntity weaponEnt)
	{
		if (m_TurretEvents && m_TurretEventsOwner && !m_TurretEventsOwner.IsDeleted())
			m_TurretEvents.RemoveScriptHandler("OnAmmoCountChanged", this, OnAmmoCountChanged);

		m_TurretEvents = null;
		m_TurretEventsOwner = null;

		if (!weaponEnt || weaponEnt.IsDeleted())
			return;

		IEntity ent = weaponEnt.GetParent();
		while (ent)
		{
			EventHandlerManagerComponent events = EventHandlerManagerComponent.Cast(ent.FindComponent(EventHandlerManagerComponent));
			if (events)
			{
				m_TurretEvents = events;
				m_TurretEventsOwner = ent;
				m_TurretEvents.RegisterScriptHandler("OnAmmoCountChanged", this, OnAmmoCountChanged);
				return;
			}

			ent = ent.GetParent();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void OnAmmoCountChanged(BaseWeaponComponent weapon, BaseMuzzleComponent muzzle, BaseMagazineComponent magazine, int ammoCount, bool isBarrelChambered)
	{
		if (!magazine || magazine == m_Magazine)
			Wake();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnShot(IEntity effectEntity, BaseMuzzleComponent muzzle, IEntity projectileEntity)
	{
		if (!m_Slot || !m_Magazine)
			return;

		if (muzzle)
		{
			BaseMagazineComponent muzzleMagazine = muzzle.GetMagazine();
			if (muzzleMagazine && muzzleMagazine != m_Magazine)
				return; // another magazine fired
		}

		float now = NowMs();
		if (now - m_fLastShotTime < m_fShotDebounce * 1000)
			return; // duplicate event for the same shot

		m_fLastShotTime = now;

		// the count drop of this shot may already be known: that drop and this event are one shot
		if (ReadCount(now) < 0)
			return; // refilled in place: snapped

		if (m_iUnmatchedDrops > 0 && now - m_fUnmatchedDropTime <= m_fMatchWindow * 1000)
		{
			m_iUnmatchedDrops--;
		}
		else if (m_bPredictOnShot)
		{
			// base on what is already shown or known, so a proxy whose count lags still steps once per shot
			int baseSpent = Math.MaxInt(m_iEventSpent, Math.MaxInt(m_iShownSpent, SpentFromCount(m_iLastCount)));
			m_iEventSpent = Math.ClampInt(baseSpent + 1, 0, m_iMax);
			m_fPredictUntil = now + m_fPredictTimeout * 1000;
		}

		if (m_bDebug)
			DebugLog("shot event");

		Wake();
	}

	//------------------------------------------------------------------------------------------------
	protected void Poll()
	{
		if (!m_Slot || !m_Magazine || m_bTicking)
			return;

		// self-heal while idle: a re-attach discards the slot's additive transform; the mesh may have been reset
		if (m_iPhase == PHASE_IDLE)
		{
			m_bNeedApply = !ApplyCellAngle(m_fShownAngle);
			ShowCellModel(m_EmptyCellVObject, true);
		}

		int count = m_Magazine.GetAmmoCount();
		bool aheadOfLiveCount = m_bCountLive && m_iShownSpent != SpentFromCount(count) && NowMs() > m_fPredictUntil;
		if (count != m_iLastCount || aheadOfLiveCount)
			Wake();
	}

	//------------------------------------------------------------------------------------------------
	protected void Wake()
	{
		if (!m_Slot || !m_Magazine || !GetGame())
			return;

		float now = NowMs();
		m_fWatchUntil = Math.Max(m_fWatchUntil, now + m_fWatchTime * 1000);

		// the weapon may have been attached to the turret after the cell was attached to the weapon
		if (!m_TurretEvents && m_WeaponEntity)
			BindTurretEvents(m_WeaponEntity);

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

		float now = NowMs();
		if (ReadCount(now) < 0)
			return; // refilled in place: snapped

		if (m_bNeedApply)
			m_bNeedApply = !ApplyCellAngle(m_fShownAngle);

		if (m_iPhase == PHASE_IDLE)
		{
			TickIdle(now);
			return;
		}

		float elapsed = now - m_fPhaseStart;

		if (m_iPhase == PHASE_SLIDE)
		{
			float slideMs = Math.Max(m_fSlideTime * 1000 * m_fStepTimeScale, 1);
			float slideT = Math.Clamp(elapsed / slideMs, 0, 1);
			SetShellTransform(m_iAnimChamber, m_fSlideDistance * slideT * slideT); // accelerating, like a fired bomb
			if (slideT >= 1)
			{
				DeleteShell(m_iAnimChamber);
				m_iPhase = PHASE_PAUSE;
				m_fPhaseStart = now;
			}

			return;
		}

		if (m_iPhase == PHASE_PAUSE)
		{
			if (elapsed >= m_fRotateDelay * 1000 * m_fStepTimeScale)
			{
				m_iPhase = PHASE_ROTATE;
				m_fPhaseStart = now;
				m_fAngleFrom = m_iShownSpent * m_fPitch;
				m_fAngleTo = (m_iShownSpent + 1) * m_fPitch;
			}

			return;
		}

		// PHASE_ROTATE
		float rotateMs = Math.Max(m_fRotateTime * 1000 * m_fStepTimeScale, 1);
		float rotateT = Math.Clamp(elapsed / rotateMs, 0, 1);
		float inv = 1.0 - rotateT;
		float ease = 1.0 - inv * inv * inv; // ease-out cubic
		SetCellAngle(m_fAngleFrom + (m_fAngleTo - m_fAngleFrom) * ease);
		if (rotateT >= 1)
		{
			m_iShownSpent++;
			int idleChamber = m_iShownSpent % m_iMax;
			SetCellAngle(idleChamber * m_fPitch);
			m_iPhase = PHASE_IDLE;
			m_fWatchUntil = Math.Max(m_fWatchUntil, now + 100);
			if (m_bDebug)
				DebugLog("step done");
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void TickIdle(float now)
	{
		int spent = SpentFromCount(m_iLastCount);
		int target = TargetSpent(now);

		if (target > m_iShownSpent)
		{
			int behind = target - m_iShownSpent;

			// a count drop soon after binding with no shot seen since (late join, reload, late count): give a shot
			// event the match window to claim it, otherwise jump instead of replaying old shots
			bool settleDrop = m_fLastShotTime < m_fBindTime && m_fUnmatchedDropTime >= m_fBindTime && (m_fUnmatchedDropTime - m_fBindTime) < m_fSettleSeconds * 1000;
			if (settleDrop)
			{
				if (now - m_fUnmatchedDropTime <= m_fMatchWindow * 1000)
					return; // keep ticking, wait for the event

				SnapToSpent(target, true);
				return;
			}

			if (behind > m_iMaxAnimatedSteps)
				SnapToSpent(target, true);
			else
				BeginStep(now, behind);

			return;
		}

		if (target < m_iShownSpent && m_bCountLive && now > m_fPredictUntil)
		{
			// ahead of a count that does replicate, with no recent shot to explain it (late duplicate event)
			if (m_bDebug)
				DebugLog("view ahead of the count: correcting");

			SnapToSpent(target, false);
			return;
		}

		if (now >= m_fWatchUntil && (m_iShownSpent == spent || now > m_fPredictUntil))
			StopTicking();
	}

	//------------------------------------------------------------------------------------------------
	//! \return -1 = count rose (view snapped), 0 = unchanged, n > 0 = rounds dropped since the last read
	protected int ReadCount(float now)
	{
		int count = m_Magazine.GetAmmoCount();
		if (count == m_iLastCount)
			return 0;

		if (count > m_iLastCount)
		{
			// refilled or reset in place: never spin backwards through the chambers, just jump
			m_fPredictUntil = NEVER;
			SnapToSpent(SpentFromCount(count), false);
			if (m_bDebug)
				DebugLog("count rose: snapped");

			return -1;
		}

		int dropped = m_iLastCount - count;
		int spentBefore = SpentFromCount(m_iLastCount);
		m_iLastCount = count;
		m_bCountLive = true;

		// rounds of this drop a shot event already announced; the rest wait for their event
		int announced = Math.ClampInt(m_iEventSpent - spentBefore, 0, dropped);
		int unmatched = dropped - announced;
		if (unmatched > 0)
		{
			if (now - m_fUnmatchedDropTime > m_fMatchWindow * 1000)
				m_iUnmatchedDrops = 0;

			m_iUnmatchedDrops += unmatched;
			m_fUnmatchedDropTime = now;
		}

		int spentNow = SpentFromCount(count);
		if (m_iEventSpent < spentNow)
			m_iEventSpent = spentNow;

		if (m_bDebug)
			DebugLog("count dropped");

		return dropped;
	}

	//------------------------------------------------------------------------------------------------
	protected int TargetSpent(float now)
	{
		int spent = SpentFromCount(m_iLastCount);
		if (m_iEventSpent > spent && now <= m_fPredictUntil)
			return m_iEventSpent;

		return spent;
	}

	//------------------------------------------------------------------------------------------------
	protected int SpentFromCount(int count)
	{
		return Math.ClampInt(m_iMax - count, 0, m_iMax);
	}

	//------------------------------------------------------------------------------------------------
	protected void BeginStep(float now, int behind)
	{
		ShowCellModel(m_EmptyCellVObject, true); // re-assert only if the prefab model provably came back

		m_fStepTimeScale = 1;
		if (behind > 1)
			m_fStepTimeScale = 0.6; // catching up

		m_iAnimChamber = ChamberForSpent(m_iShownSpent);
		m_fPhaseStart = now;
		if (m_aShells.IsIndexValid(m_iAnimChamber) && m_aShells[m_iAnimChamber])
			m_iPhase = PHASE_SLIDE;
		else
			m_iPhase = PHASE_PAUSE; // chamber already shown empty: just turn

		if (m_bDebug)
			DebugLog("step start");
	}

	//------------------------------------------------------------------------------------------------
	//! Jump without animation: bind, reload, refill, late join, big lag, correction.
	//! \param keepUnmatched keep count drops that a late shot event may still claim (lag / settle snaps)
	protected void SnapToSpent(int spent, bool keepUnmatched)
	{
		m_iPhase = PHASE_IDLE;
		m_iLastCount = m_Magazine.GetAmmoCount();
		m_iShownSpent = Math.ClampInt(spent, 0, m_iMax);

		int countSpent = SpentFromCount(m_iLastCount);
		m_iEventSpent = Math.MaxInt(m_iShownSpent, countSpent);
		if (!keepUnmatched)
			m_iUnmatchedDrops = 0;

		if (m_iShownSpent <= countSpent)
			m_fPredictUntil = NEVER;

		DeleteAllShells();
		for (int j = m_iShownSpent; j < m_iMax; j++)
		{
			SpawnShell(ChamberForSpent(j));
		}

		int resyncChamber = m_iShownSpent % m_iMax;
		SetCellAngle(resyncChamber * m_fPitch);
	}

	//------------------------------------------------------------------------------------------------
	//! Chamber in line with the barrel after j rounds are gone.
	protected int ChamberForSpent(int j)
	{
		int k = j % m_iMax;
		if (m_bRiseFromLeft)
			return (m_iMax - k) % m_iMax;

		return k;
	}

	//------------------------------------------------------------------------------------------------
	//! \param reassert true = periodic check: only acts when comparing model handles is proven reliable
	protected void ShowCellModel(VObject model, bool reassert)
	{
		if (!model)
			return;

		IEntity owner = GetOwner();
		if (!owner || owner.IsDeleted())
			return;

		if (reassert)
		{
			if (m_CurrentModel != model || !m_bModelCompareWorks || owner.GetVObject() == model)
				return;
		}
		else if (m_CurrentModel == model)
		{
			if (!m_bModelCompareWorks || owner.GetVObject() == model)
				return;
		}

		// vanilla SCR_ConsumableItemComponent swaps an item's model the same way
		owner.SetObject(model, string.Empty);
		m_CurrentModel = model;
		m_bModelCompareWorks = (owner.GetVObject() == model);
	}

	//------------------------------------------------------------------------------------------------
	//! Loose cell: prefab model (five baked bombs), or the empty model when no round is left.
	protected void ApplyLooseLook()
	{
		if (!m_Magazine)
			return;

		if (m_bEmptyLookWhenSpent && m_EmptyCellVObject && m_Magazine.GetAmmoCount() <= 0)
		{
			ShowCellModel(m_EmptyCellVObject, false);
			return;
		}

		if (m_CurrentModel && m_CurrentModel != m_LoadedCellVObject)
			ShowCellModel(m_LoadedCellVObject, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void SpawnShell(int chamber)
	{
		if (!m_ShellPrefabResource || !m_aShells.IsIndexValid(chamber) || m_aShells[chamber])
			return;

		IEntity owner = GetOwner();
		if (!owner || owner.IsDeleted())
			return;

		// vanilla SCR_LoiterItemData: local prefab spawn at the owner, AddChild, SetLocalTransform
		EntitySpawnParams spawnParams = new EntitySpawnParams();
		owner.GetTransform(spawnParams.Transform);
		IEntity shell = GetGame().SpawnEntityPrefabLocal(m_ShellPrefabResource, owner.GetWorld(), spawnParams);
		if (!shell)
			return;

		shell.ClearFlags(EntityFlags.TRACEABLE, false); // never block traces or user actions
		shell.SetFlags(EntityFlags.VISIBLE, false);
		owner.AddChild(shell, -1);
		m_aShells[chamber] = shell;
		SetShellTransform(chamber, 0);
	}

	//------------------------------------------------------------------------------------------------
	//! Bomb in chamber k, pushed 'slide' metres toward the muzzle. Parent = the cell, so the cell's turn carries it.
	protected void SetShellTransform(int chamber, float slide)
	{
		if (!m_aShells.IsIndexValid(chamber))
			return;

		IEntity shell = m_aShells[chamber];
		if (!shell || shell.IsDeleted())
			return;

		float th = chamber * m_fPitch * Math.DEG2RAD;
		vector mat[4];
		mat[0] = Vector(1, 0, 0);	// shell x -> cell x
		mat[1] = Vector(0, 0, -1);	// shell y -> cell -z
		mat[2] = Vector(0, 1, 0);	// shell z (nose) -> cell +y (toward the muzzle); det = +1
		mat[3] = Vector(m_fChamberRadius * Math.Sin(th), m_fShellSeat + slide, -m_fChamberRadius * Math.Cos(th));
		shell.SetLocalTransform(mat);
		shell.Update();
	}

	//------------------------------------------------------------------------------------------------
	protected void DeleteShell(int chamber)
	{
		if (!m_aShells.IsIndexValid(chamber))
			return;

		IEntity shell = m_aShells[chamber];
		m_aShells[chamber] = null;
		DisposeShell(shell);
	}

	//------------------------------------------------------------------------------------------------
	protected void DeleteAllShells()
	{
		for (int i = 0; i < m_aShells.Count(); i++)
		{
			IEntity shell = m_aShells[i];
			m_aShells[i] = null;
			DisposeShell(shell);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void DisposeShell(IEntity shell)
	{
		if (!shell || shell.IsDeleted())
			return;

		shell.ClearFlags(EntityFlags.VISIBLE, false);

		IEntity parent = shell.GetParent();
		if (parent)
			parent.RemoveChild(shell, true);

		// vanilla deletes spawned helpers later "to ensure that the entity is deleted from a right thread"
		if (GetGame())
			GetGame().GetCallqueue().CallLater(SCR_EntityHelper.DeleteEntityAndChildren, 0, false, shell);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetCellAngle(float advanceDeg)
	{
		m_fShownAngle = advanceDeg;
		m_bNeedApply = !ApplyCellAngle(advanceDeg);
	}

	//------------------------------------------------------------------------------------------------
	//! Turn the cell about its own +Y axis. A positive advance moves a chamber from angle th to th + advance,
	//! th measured from -Z (in line with the bore) toward +X. Built explicitly, no Euler convention involved.
	protected bool ApplyCellAngle(float advanceDeg)
	{
		IEntity owner = GetOwner();
		if (!m_Slot || !owner || m_Slot.GetAttachedEntity() != owner)
			return false; // mid-swap: never turn somebody else's cell

		float d = advanceDeg * Math.DEG2RAD;
		if (!m_bRiseFromLeft)
			d = -d;

		float c = Math.Cos(d);
		float s = Math.Sin(d);

		vector rot[4];
		rot[0] = Vector(c, 0, s);
		rot[1] = Vector(0, 1, 0);
		rot[2] = Vector(-s, 0, c);
		rot[3] = vector.Zero;

		if (m_bDriveEntityDirectly)
		{
			vector slotLocal[4];
			m_Slot.GetLocalTransform(slotLocal);
			vector local[4];
			Math3D.MatrixMultiply4(slotLocal, rot, local); // local(p) = slot(rot(p)): turn about the cell origin
			owner.SetLocalTransform(local);
			owner.Update();
		}
		else
		{
			m_Slot.SetAdditiveTransformLS(rot);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected float NowMs()
	{
		if (!GetGame() || !GetGame().GetWorld())
			return 0;

		return GetGame().GetWorld().GetWorldTime();
	}

	//------------------------------------------------------------------------------------------------
	protected void DebugLog(string what)
	{
		PrintFormat("STC_MortarCellVisual %1: %2 | count %3 shown %4 event %5 unmatched %6 phase %7", GetOwner(), what, m_iLastCount, m_iShownSpent, m_iEventSpent, m_iUnmatchedDrops, m_iPhase);
	}
}
